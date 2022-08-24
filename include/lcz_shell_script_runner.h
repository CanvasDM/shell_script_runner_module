/**
 * @file lcz_shell_script_runner.h
 *
 * Copyright (c) 2022 Laird Connectivity LLC
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

#ifndef __LCZ_SHELL_SCRIPT_RUNNER_H__
#define __LCZ_SHELL_SCRIPT_RUNNER_H__

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************/
/* Global Function Prototypes                                                                     */
/**************************************************************************************************/

/**
 * @brief Run a shell script.
 * Shell script output is logged to the file system as \p path with the .out suffix.
 * The script will stop running on the first error it encounters.
 *
 * @param path full path to the script file
 * @param shell shell to print command responses to. If NULL, no command responses will be printed.
 * @return int 0 on success < 0 on error
 */
int lcz_zsh_run_script(const char* path, const struct shell *shell);

#ifdef __cplusplus
}
#endif

#endif /* __LCZ_SHELL_SCRIPT_RUNNER_H__ */
