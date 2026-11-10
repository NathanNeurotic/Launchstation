/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
/* NTLMv2 */

#ifdef USMB2_FEATURE_NTLM

#if defined(_IOP)
#include "ps2iop-compat.h"
#else
#include <endian.h>
#endif

#if defined(Z80)
#define le16toh(x) (x)
#define le32toh(x) (x)
#define htole16(x) (x)
#define htole32(x) (x)
#endif

#include "usmb2.h"
#include "ntlm.h"
#include "md4.h"
#include "hmac-md5.h"

#include <stdio.h>

/* We are trying to re-use the same buffer we use for reading and writing to the socket as much as possible.
 * Makes the code hairy but saves several hundred bytes of buffers.
 * There are three things we nee from the original CHALLENGE message which was part of the previous reply
 * we goot and which is stored un usmb2->buf.
 * The server challenge, the TargetName and the TargetInfo. Are we are re-building the next SessionSetup request
 * within the same buffer as the reply we need data from, we must be careful so that we copy the data from their old
 * location in the reply to the new location in the to-be request before we overwrite that memory with new content.
 */
/* When this function is called the buffer contains
 * SessionSetup reply header: 8 bytes
 * Incoming NTLMSSP SecurityBuffer: hundreds of bytes.
 *
 * We are rewriting this in place to be
 * SPL                             : 4 bytes, to be filled in later
 * SMB2 header                     : 64 bytes, to be filled in later
 * SessionSetup Request header     : 24 bytes, to be filled in later
 * Outgoing NTLMSSP SecurityBuffer : hundreds of bytes. To be filled in by this function.
 */
int ntlm_generate_auth(struct usmb2_context *usmb2,
                       char *username,
                       char *password)
{
        uint8_t NTOWFv2[16], server_challenge[8], z = 0;
        uint16_t ntlmssp_in_offset, ntlmssp_out_offset, offset;
        uint16_t domain_name_offset, domain_name_len; char *domain_name;
        uint16_t user_name_offset, user_name_len;
        uint16_t ntlm_response_offset;
        uint16_t at_type, at_len, out_pdu_size, target_info_len;
        char *target_info;
        /* RiptOPL: the CHALLENGE's fields are bounded by what it holds before anything is read or written */
        uint32_t sec_off, sec_len, dn_off, ti_off, ti_len;

        sec_off = le16toh(*(uint16_t *)&usmb2->buf[4]);
        sec_len = le16toh(*(uint16_t *)&usmb2->buf[6]);
        if (sec_off < 64 + 8 || (sec_off & 3) || sec_len < 48 || usmb2->reply_len > sizeof(usmb2->buf) ||
            sec_off - 64 + sec_len > usmb2->reply_len) {
                return -1;
        }

        /* Set offset to start of ntlmssp blob */
        ntlmssp_in_offset = sec_off - 64;
        ntlmssp_out_offset = 4 + 64 + 24;
 
        /* Get offset to start of TargetName */
        offset = ntlmssp_in_offset + 12; /* skip past NTLMSSP signature and message type */

        /* Grab Target/Domain name from the challenge buffer */
        domain_name_len = le16toh(*(uint16_t *)&usmb2->buf[offset]);
        dn_off = le32toh(*(uint32_t *)&usmb2->buf[offset + 4]);
        ti_len = le16toh(*(uint16_t *)&usmb2->buf[ntlmssp_in_offset + 40]);
        ti_off = le32toh(*(uint32_t *)&usmb2->buf[ntlmssp_in_offset + 44]);
        /* ... and aligned as servers send them: the IOP faults on a misaligned halfword or word */
        if ((dn_off & 1) || (ti_off & 1) || (domain_name_len & 1) ||
            dn_off > sec_len || domain_name_len > sec_len - dn_off || ti_off > sec_len || ti_len > sec_len - ti_off) {
                return -1;
        }
        domain_name = (char *)&usmb2->buf[ntlmssp_in_offset + dn_off];
        domain_name_offset = 72; /* This is where AUTH ends on pre-2003 */
        user_name_len = strlen(username) * 2;
        user_name_offset = domain_name_len + domain_name_offset;

        ntlm_response_offset = user_name_len + user_name_offset; /* relative to start of NTLMSSP */

 
        /* SessionSetup payload, i.e. NTLMSSP will start at the buffer offset 64 + 24 */
         
        /* Start by moving the TargetInfo buffer we received from the server in the CHALLENGE message
         * to where it will end up at the end of the auth message we are building.
         * Depending on the size of the TargetName, this shifts the offset into the NTLMSSP
         * buffer, but it is usually in the region 70-80 bytes for shortish target names.
         * In the AUTH we are building the LmChallengeResponse data often ends up laid
         * your starting at offset ~110 and the AvPairs are laid out another 16 + 28 bytes into that.
         */
        target_info = (char *)&usmb2->buf[ntlmssp_in_offset + ti_off];

        /* Copy everything except the trailing EndOfList.
         * The length does not fit in a char: servers in a domain send 130+ bytes.
         * RiptOPL: only within TargetInfoLen. The list also ends where it does without an
         * EndOfList, and before a pair that overruns it, as the in-game reader ends it, or that
         * has an odd length (the next pair would be misaligned).
         */
        target_info_len = 0;
        while(1) {
                if ((uint32_t)target_info_len + 4 > ti_len) {
                        break;
                }
                at_type = le16toh(*(uint16_t *)&target_info[target_info_len]);
                at_len = le16toh(*(uint16_t *)&target_info[target_info_len + 2]);
                if (at_type == 0 || (at_len & 1) || at_len > ti_len - target_info_len - 4) {
                        break;
                }
                target_info_len += 4 + at_len;
        }
        /* RiptOPL: the AUTHENTICATE, padded as usmb2_sessionsetup pads it, must fit in buf
         * (computed wide: the 16-bit offsets below would wrap first).
         */
        if (4 + 64 + 24 + 7 + (uint32_t)ntlm_response_offset + 16 + 28 + target_info_len + 4 + 10 + domain_name_len + 8 >
            sizeof(usmb2->buf)) {
                return -1;
        }
        /* RiptOPL: the Server Challenge, saved before the TargetInfo moves: where a server puts
         * the CHALLENGE's security buffer far into the reply, the move lands on it.
         */
        memcpy(server_challenge, &usmb2->buf[ntlmssp_in_offset + 24], 8);
        offset = 4 + 64 + 24 + ntlm_response_offset + 16 + 28; // AvPairs
        /* The new place overlaps the old one when the TargetInfo is longer than
         * about 144 + user_name_len bytes, so memmove and not memcpy.
         */
        memmove(&usmb2->buf[offset], target_info, target_info_len);

        /* Get offset to start of Server Challenge and copy it just before where 'temp'
           ends up in the response so we can compute the hmac-md5 as linear buffer.
           Only now that the TargetInfo is out of the way: this lands in it when it is
           longer than about 108 + user_name_len bytes.
         */
        memcpy(&usmb2->buf[4 + 64 + 24 + ntlm_response_offset + 8], server_challenge, 8);
        
        /* Add TargetName Av entry to the end */
        offset += target_info_len;
        *(uint16_t *)&usmb2->buf[offset] = htole16(9);
        *(uint16_t *)&usmb2->buf[offset + 2] = htole16(10 + domain_name_len);
        offset += 4;
        *(uint16_t *)&usmb2->buf[offset] = htole16('c');
        offset += 2;
        *(uint16_t *)&usmb2->buf[offset] = htole16('i');
        offset += 2;
        *(uint16_t *)&usmb2->buf[offset] = htole16('f');
        offset += 2;
        *(uint16_t *)&usmb2->buf[offset] = htole16('s');
        offset += 2;
        *(uint16_t *)&usmb2->buf[offset] = htole16('/');
        offset += 2;
        memcpy(&usmb2->buf[offset], domain_name, domain_name_len);
        offset += domain_name_len;
        
        /* and a final EndOfList plus 4 bytes of padding*/
        memset(&usmb2->buf[offset], 0, 8);
        offset += 8;
        out_pdu_size = offset;
                
        /*
         * Domain name. Copy to proper offset in the output  buffer and update length/offset.
         */
        /* This overlaps when the domain name is longer than 100 bytes */
        memmove(&usmb2->buf[4 + 64 + 24 + domain_name_offset], domain_name, domain_name_len);
        domain_name = (char *)&usmb2->buf[4 + 64 + 24 + domain_name_offset];
        *(uint16_t *)&usmb2->buf[ntlmssp_out_offset + 28] = htole16(domain_name_len);
        *(uint16_t *)&usmb2->buf[ntlmssp_out_offset + 30] = htole16(domain_name_len);
        *(uint32_t *)&usmb2->buf[ntlmssp_out_offset + 32] = htole32(domain_name_offset);

        /*
         * NT Response
         */
        at_type = out_pdu_size - 4 - 64 - 24 - ntlm_response_offset;
        *(uint16_t *)&usmb2->buf[ntlmssp_out_offset + 20] = htole16(at_type);
        *(uint16_t *)&usmb2->buf[ntlmssp_out_offset + 22] = htole16(at_type);
        *(uint32_t *)&usmb2->buf[ntlmssp_out_offset + 24] = htole32(ntlm_response_offset);
          
        /*
         * User name
         */
        /* RiptOPL: two separate stores; upstream read and incremented at_type unsequenced */
        for(at_type = 0; at_type < user_name_len; at_type += 2) {
                usmb2->buf[4 + 64 + 24 + user_name_offset + at_type] = username[at_type >> 1];
                usmb2->buf[4 + 64 + 24 + user_name_offset + at_type + 1] = 0;
        }
        *(uint16_t *)&usmb2->buf[ntlmssp_out_offset + 36] = htole16(user_name_len);
        *(uint16_t *)&usmb2->buf[ntlmssp_out_offset + 38] = htole16(user_name_len);
        *(uint32_t *)&usmb2->buf[ntlmssp_out_offset + 40] = htole32(user_name_offset);
        /*
         * Host name and remaining fields
         */
        memset(&usmb2->buf[ntlmssp_out_offset + 44], 0, 12);

        /* Negotiate flags */
        *(uint32_t *)&usmb2->buf[ntlmssp_out_offset + 56] = htole32(0x20088037);

        /* Version */
        memset(&usmb2->buf[ntlmssp_out_offset + 60], 0, 8);

        /* At this point we no longer need to reference anything from the incoming security buffer
         * so we can use the early parts of the buffer as a scratch area. 4 + 64 + 24 + 20 bytes.
         */
        
        /* Generate NTOWFv1 */
        /* The MD4 context is 64 bytes so it fits in the scratch area
        * Store NTOWFv1 at offset 4 + 64 + 24 + 4 */
        MD4Init((MD4_CTX *)&usmb2->buf[0]);
        z = 0;
        while (*password) {
                MD4Update((MD4_CTX *)&usmb2->buf[0], (unsigned char *)password++, 1);
                MD4Update((MD4_CTX *)&usmb2->buf[0], &z, 1);
        }
        MD4Final(&usmb2->buf[4 + 64 + 24 + 4], (MD4_CTX *)&usmb2->buf[0]);

        /* Compute NTOWFv2 */
        /* We don't have enough scratch area to store NTOWFv2 too :-( */
        hmac_md5((struct MD5Context *)&usmb2->buf[0],
                 (uint8_t *)username,
                 NULL, 0,
                 (uint8_t *)domain_name, domain_name_len,
                 &usmb2->buf[4 + 64 + 24 + 4], 16, NTOWFv2);

        /*
         * Clear beginning of ntlmv2 response and fill in all non-zero parts
         */
        memset(&usmb2->buf[4 + 64 + 24 + ntlm_response_offset + 16], 0, 28);
        /* resp type and hi resp type */
        *(uint16_t *)&usmb2->buf[4 + 64 + 24 + ntlm_response_offset + 16] = 0x0101;
        /* client challenge QQQQ */
        memset(&usmb2->buf[4 + 64 + 24 + ntlm_response_offset + 32], 1, 8);
        
        
        at_type = 4 + 64 + 24 + ntlm_response_offset;
        at_len = out_pdu_size - 4 - 64 - 24 - ntlm_response_offset;
        
        hmac_md5((struct MD5Context *)&usmb2->buf[0],
                 NULL,
                 &usmb2->buf[at_type + 8],  at_len - 8,
                 NULL, 0,
                 NTOWFv2, 16, &usmb2->buf[4 + 64 + 24 + ntlm_response_offset]);

        /* RiptOPL: the session base key, HMAC_MD5(NTOWFv2, NTProofStr), which signs (no KEY_EXCH) */
        hmac_md5((struct MD5Context *)&usmb2->buf[0],
                 NULL,
                 &usmb2->buf[4 + 64 + 24 + ntlm_response_offset], 16,
                 NULL, 0,
                 NTOWFv2, 16, usmb2->session_key);


        /* Fill in the first 20 bytes of the NTLMSSP buffer */
        memset(&usmb2->buf[4 + 64 + 24 + 8], 0, 12);
        usmb2->buf[4 + 64 + 24 + 8] = 3;
        
        return out_pdu_size - 4 - 64 - 24;
}

#endif /* USMB2_FEATURE_NTLM */
