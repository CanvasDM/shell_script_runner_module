/**
 * @file lcz_shell_script_runner.c
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <logging/log.h>
LOG_MODULE_REGISTER(lcz_zsh, CONFIG_LCZ_SHELL_SCRIPT_RUNNER_LOG_LEVEL);

#include <zephyr.h>
#include <fs/fs.h>
#include <shell/shell.h>
#include <shell/shell_dummy.h>
#if defined(CONFIG_LCZ_SHELL_LOGIN)
#include <lcz_shell_login.h>
#endif
#if defined(CONFIG_ATTR)
#include <attr.h>
#endif
#include "lcz_shell_script_runner.h"

/**************************************************************************************************/
/* Local Constant, Macro and Type Definitions                                                     */
/**************************************************************************************************/
#if defined(CONFIG_LCZ_SHELL_LOGIN)
#define SHELL_LOGOUT_CMD "logout"
#endif
#define SHELL_OUTPUT_FILE_SUFFIX ".out"
#define COMMENT_START '#'

/**************************************************************************************************/
/* Local Function Prototypes                                                                      */
/**************************************************************************************************/
static bool is_crlf(uint8_t c);
static int cmd_run_script(const struct shell *shell, size_t argc, char **argv);
#if defined(CONFIG_LCZ_SHELL_LOGIN)
static bool login_if_needed(void);
static int logout(void);
#endif

/**************************************************************************************************/
/* Local Function Definitions                                                                     */
/**************************************************************************************************/
#if defined(CONFIG_LCZ_SHELL_LOGIN)
static bool login_if_needed(void)
{
	int ret;
	bool did_login = false;
	char *password;

#if defined(CONFIG_SHELL_LOGIN_PASSWORD)
	password = CONFIG_SHELL_LOGIN_PASSWORD;
#else
	password = (char *)attr_get_quasi_static(ATTR_ID_shell_password);
#endif

	if (!lcz_shell_login_is_logged_in()) {
		ret = shell_execute_cmd(NULL, password);
		if (ret >= 0) {
			did_login = true;
		}
	}

	return did_login;
}

static int logout(void)
{
	return shell_execute_cmd(NULL, SHELL_LOGOUT_CMD);
}
#endif /* CONFIG_LCZ_SHELL_LOGIN */

static bool is_crlf(uint8_t c)
{
	if (c == '\n' || c == '\r') {
		return true;
	} else {
		return false;
	}
}

static int cmd_run_script(const struct shell *shell, size_t argc, char **argv)
{
	int ret;

	ret = lcz_zsh_run_script((const char *)argv[1], shell);
	if (ret < 0) {
		shell_error(shell, "Error [%d]", ret);
	} else {
		shell_print(shell, "Ok");
	}
	return ret;
}

/**************************************************************************************************/
/* Global Function Definitions                                                                    */
/**************************************************************************************************/
int lcz_zsh_run_script(const char *path, const struct shell *shell)
{
	const struct shell *dummy_shell;
	char cmd_buf[CONFIG_LCZ_ZSH_CMD_MAX_SIZE];
	char result_file_path[CONFIG_LCZ_ZSH_PATH_MAX_SIZE + sizeof(SHELL_OUTPUT_FILE_SUFFIX)];
	const char *cmd_resp_buf;
	size_t cmd_resp_size;
	struct fs_file_t script;
	struct fs_file_t result_file;
	int cmd_err = 0;
	int bytes_read;
	bool read_file = false;
	bool script_err = false;
#if defined(CONFIG_LCZ_SHELL_LOGIN)
	bool logout_user = false;
#endif
	int ret;

#if defined(CONFIG_LCZ_SHELL_LOGIN)
	logout_user = login_if_needed();
#endif

	/* create and override result file */
	fs_file_t_init(&result_file);
	ret = snprintk(result_file_path, sizeof(result_file_path), "%s%s", path,
		       SHELL_OUTPUT_FILE_SUFFIX);
	if (ret < 0) {
		LOG_ERR("Could not create result file path [%d]", ret);
		goto done;
	}
	if (ret >= sizeof(result_file_path)) {
		LOG_ERR("Result file truncated [%s]", result_file_path);
		ret = -EINVAL;
		goto done;
	}
	LOG_DBG("Creating result file %s", result_file_path);
	ret = fs_open(&result_file, result_file_path, FS_O_WRITE | FS_O_CREATE);
	if (ret < 0) {
		LOG_ERR("Could not open %s", result_file_path);
		goto done;
	}

	/* open file */
	fs_file_t_init(&script);
	ret = fs_open(&script, path, FS_O_READ);
	if (ret < 0) {
		LOG_ERR("Could not open %s", path);
		goto close_result_file;
	}

	LOG_INF("Running script %s", path);
	dummy_shell = shell_backend_dummy_get_ptr();
	read_file = true;
	while (read_file) {
		bytes_read = 0;
		cmd_buf[0] = 0;
		cmd_err = 0;
		/* read one line */
		do {
			ret = fs_read(&script, cmd_buf + bytes_read, 1);
			if (ret < 0) {
				script_err = true;
				goto close;
			} else if (ret == 0) {
				read_file = false;
			}
			bytes_read += ret;
		} while ((bytes_read < CONFIG_LCZ_ZSH_CMD_MAX_SIZE) &&
			 !is_crlf(cmd_buf[bytes_read - 1]) && read_file);

		/* replace crlf with null char to terminate cmd string */
		cmd_buf[bytes_read - 1] = 0;

		if (strlen(cmd_buf) < 1 || cmd_buf[0] == COMMENT_START) {
			continue;
		}

		LOG_DBG("Executing [%s]", cmd_buf);
		shell_backend_dummy_clear_output(dummy_shell);
		cmd_err = shell_execute_cmd(NULL, cmd_buf);
		cmd_resp_buf = shell_backend_dummy_get_output(dummy_shell, &cmd_resp_size);
		LOG_DBG("Result:\n\rReturn: %d\n\rResp size: %d\n\rresp: %s", cmd_err,
			cmd_resp_size, cmd_resp_buf);
		if (shell != NULL) {
			if (cmd_err < 0) {
				shell_error(shell, "%s", cmd_resp_buf);
			} else {
				shell_print(shell, "%s", cmd_resp_buf);
			}
		}
		if (cmd_err < 0) {
			script_err = true;
			(void)snprintk(cmd_buf, sizeof(cmd_buf), "Err: %d\n", cmd_err);
			ret = fs_write(&result_file, cmd_buf, strlen(cmd_buf));
			if (ret < 0) {
				LOG_ERR("Could not write to %s [%d]", result_file_path, ret);
				script_err = true;
				goto close;
			}
		}
		ret = fs_write(&result_file, cmd_resp_buf, cmd_resp_size);
		if (ret < 0) {
			LOG_ERR("Could not write to %s [%d]", result_file_path, ret);
			script_err = true;
			goto close;
		}
		if (script_err) {
			/* stop running script on cmd error */
			break;
		}
	}

#if defined(CONFIG_LCZ_SHELL_LOGIN)
	if (logout_user) {
		ret = logout();
	}
#endif

close:
	ret = fs_close(&script);
	if (ret < 0) {
		LOG_ERR("Could not close %s", path);
	}
close_result_file:
	ret = fs_close(&result_file);
	if (ret < 0) {
		LOG_ERR("Could not close %s", result_file_path);
	}
done:

	if (script_err || ret != 0) {
		LOG_ERR("%s failed", path);
		return cmd_err ? cmd_err : (ret ? ret : -EIO);
	} else {
		LOG_INF("%s finished!", path);
		return ret;
	}
}

SHELL_CMD_ARG_REGISTER(zsh, NULL, "Run shell script", cmd_run_script, 2, 0);
