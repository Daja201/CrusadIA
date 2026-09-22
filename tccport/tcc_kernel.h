#ifndef TCC_KERNEL_H
#define TCC_KERNEL_H

/* Compile a C file (from the current directory of the active filesystem) into
 * memory and run its main(argc, argv).
 * Returns 0 if the program was compiled and ran (its exit code is stored in
 * *exit_code, may be NULL), or -1 if compilation/linking failed. */
int tcc_os_run_file(const char *path, int argc, char **argv, int *exit_code);

/* Same, but from a string in memory. `name` is only used in messages. */
int tcc_os_run_source(const char *name, const char *source, int argc, char **argv, int *exit_code);

/* Compile a C file into a relocatable ELF object file and write it to
 * out_path through CrusadIA's own filesystem (fs.c), i.e. onto the drive.
 * Returns 0 on success, -1 on compile or write failure. */
int tcc_os_compile_obj_file(const char *path, const char *out_path);

/* Same, but from a string in memory. `name` is only used in messages. */
int tcc_os_compile_obj_source(const char *name, const char *source, const char *out_path);

/* Shell command:  cc <file.c> [args...]      cc -e "<C source>"
 *                 cc -c <file.c> -o <out.o>  (writes the .o to the drive) */
void cmd_cc(int argc, char **argv);

#endif