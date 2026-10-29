# Updater trust bundle

`WLE-UPDATE-CA.PEM` is packaged beside both controller variants' ELF. It is
read at runtime for GitHub API checks and HTTPS package downloads. These
are public root certificates; do not replace them with a server leaf certificate.

Reviewed October 26, 2026. SHA-256 fingerprints below identify the DER
certificates, independently of PEM whitespace or line endings.

| Root | DER SHA-256 | Role |
| --- | --- | --- |
| Sectigo Public Server Authentication Root E46 | `c90f26f0fb1b4018b22227519b5ca2b53e2ca5b3be5cf18efe1bef47380c5383` | Observed GitHub API and website ECC certificate chains. |
| Sectigo Public Server Authentication Root R46 | `7bb647a62aeeac88bf257aa522d01ffea395e0ab45c73f93f65654ec38f25a06` | Retained Sectigo RSA root. |
| ISRG Root X1 | `96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6` | Observed GitHub release-asset download chains. |

Sectigo publishes the roots in its [root hierarchy guide](https://www.sectigo.com/knowledge-base/detail/Access-New-Sectigo-Certificate-Chain).
Root E46 was downloaded from its published certificate URL,
`http://crt.sectigo.com/SectigoPublicServerAuthenticationRootE46.crt`, and its
DER bytes were checked against the fingerprint above and the distribution's
trusted self-signed Root E46. See [Let's Encrypt's certificate inventory](https://letsencrypt.org/certificates/) for ISRG Root X1.

The previous bundle lacked E46. A live check reproduced an issuer trust
failure for `api.github.com` and `github.com`; adding E46 allowed both to
verify. Both `release-assets.githubusercontent.com` and
`objects.githubusercontent.com` also verified with this bundle. These
observations do not guarantee future certificate chains will stay the same.

When changing the bundle, verify root fingerprints/provenance, repeat live
checks against API, website and actual release redirect hosts, and run
`tests/test_https_tls.py` plus the package/updater tests. Certificate/hostname/date
verification stays required. Do not add an unauthenticated fallback.

The updater preserves existing CA sidecars. To repair an older installation,
copy the corrected packaged CA file beside the boot ELF as well as installing
the corrected ELF; an ELF-only replacement leaves the old trust bundle in use.
