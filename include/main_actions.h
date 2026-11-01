#ifndef MAIN_ACTIONS_H
#define MAIN_ACTIONS_H

/* Optional policy for one ordinary ELF launch; zero retains the saved setting. */
enum {
	MAIN_ELF_IOP_DEFAULT = 0,
	MAIN_ELF_IOP_KEEP,
	MAIN_ELF_IOP_REBOOT
};

typedef struct
{
	int boot_argc;
	char **boot_argv;
	const char *boot_path;
	char *main_msg;
	char *cnf_path;
	const char *default_esr_path;
	const char *default_osdsys_path;
	const char *default_osdsys_path2;
	char rough_region;
	char *romver_data;
	int elf_iop_mode; /* MAIN_ELF_IOP_*; copy the context for a one-launch override */
} MainExecuteContext;

void ExecuteMainAction(char *pathin, const MainExecuteContext *ctx);
void ExecuteBrowserFrom(const char *start_dir, const MainExecuteContext *ctx);
extern void (*mainActionLaunchHook)(const char *path);

#endif
