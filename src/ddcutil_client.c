#include "ddcutil_client.h"

#include <gio/gio.h>
#include <glib.h>

static gboolean run_ddcutil(const gchar *const *argv, gchar **stdout_str, GError **error) {
    gchar *standard_output = NULL;
    gchar *standard_error = NULL;
    gint status = 0;

    g_auto(GStrv) spawn_argv = g_strdupv((gchar **)argv);
    if (!spawn_argv) {
        g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_FAILED,
                    "Failed to allocate argument vector for ddcutil");
        return FALSE;
    }

    if (!g_spawn_sync(NULL,
                      spawn_argv,
                      NULL,
                      G_SPAWN_SEARCH_PATH,
                      NULL,
                      NULL,
                      stdout_str ? &standard_output : NULL,
                      &standard_error,
                      &status,
                      error)) {
        g_free(standard_output);
        g_free(standard_error);
        return FALSE;
    }

    if (!g_spawn_check_wait_status(status, error)) {
        if (standard_output) {
            g_free(standard_output);
        }
        if (standard_error && *standard_error) {
            g_prefix_error(error, "%s", standard_error);
        }
        g_free(standard_error);
        return FALSE;
    }

    if (stdout_str) {
        *stdout_str = standard_output ? standard_output : g_strdup("");
    } else {
        g_free(standard_output);
    }
    g_free(standard_error);
    return TRUE;
}

static gchar *extract_value(const gchar *line) {
    const gchar *colon = strchr(line, ':');
    if (!colon) {
        return g_strdup(g_strstrip((gchar *)line));
    }
    colon++;
    while (g_ascii_isspace(*colon)) {
        colon++;
    }
    return g_strdup(colon);
}

static gchar *parse_display_id(const gchar *line) {
    const gchar *cursor = line;
    while (*cursor && !g_ascii_isdigit(*cursor)) {
        cursor++;
    }
    const gchar *start = cursor;
    while (*cursor && g_ascii_isdigit(*cursor)) {
        cursor++;
    }
    if (start == cursor) {
        return g_strdup("1");
    }
    return g_strndup(start, cursor - start);
}

static gint find_code_index(const guint8 *codes, guint n_codes, guint8 code) {
    for (guint i = 0; i < n_codes; i++) {
        if (codes[i] == code) {
            return (gint)i;
        }
    }
    return -1;
}

GPtrArray *ddcutil_list_monitors(GError **error) {
    const gchar *argv[] = {"ddcutil", "detect", "--brief", NULL};
    gchar *output = NULL;

    if (!run_ddcutil(argv, &output, error)) {
        return NULL;
    }

    GPtrArray *monitors = g_ptr_array_new_with_free_func((GDestroyNotify)ddcutil_monitor_free);
    gchar **lines = g_strsplit(output, "\n", -1);
    g_free(output);

    DdcutilMonitor *current = NULL;
    for (gint i = 0; lines[i]; i++) {
        gchar *line = g_strstrip(lines[i]);
        if (*line == '\0') {
            continue;
        }

        if (g_str_has_prefix(line, "Display")) {
            if (current) {
                if (!current->name) {
                    current->name = g_strdup("Unknown display");
                }
                g_ptr_array_add(monitors, current);
            }
            current = g_new0(DdcutilMonitor, 1);
            current->display_id = parse_display_id(line);
        } else if (g_str_has_prefix(line, "Model")) {
            g_clear_pointer(&current->name, g_free);
            current->name = extract_value(line);
        } else if (g_str_has_prefix(line, "I2C bus")) {
            g_clear_pointer(&current->bus, g_free);
            current->bus = extract_value(line);
        } else if (g_str_has_prefix(line, "Serial number")) {
            g_clear_pointer(&current->serial, g_free);
            current->serial = extract_value(line);
        }
    }

    if (current) {
        if (!current->name) {
            current->name = g_strdup("Unknown display");
        }
        g_ptr_array_add(monitors, current);
    }

    g_strfreev(lines);

    if (monitors->len == 0) {
        g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_NOT_FOUND,
                    "No DDC-capable monitors detected. Ensure your user has i2c permissions.");
        g_ptr_array_free(monitors, TRUE);
        return NULL;
    }

    return monitors;
}

static gboolean parse_vcp_response(const gchar *output, gint *current, gint *maximum, GError **error) {
    GRegex *regex = g_regex_new("current value\\s*=\\s*(\\d+).+max value\\s*=\\s*(\\d+)",
                                 G_REGEX_DOTALL | G_REGEX_CASELESS,
                                 0,
                                 NULL);
    GMatchInfo *match_info = NULL;
    gboolean matched = g_regex_match(regex, output, 0, &match_info);
    if (matched) {
        gchar *current_str = g_match_info_fetch(match_info, 1);
        gchar *max_str = g_match_info_fetch(match_info, 2);
        if (current) {
            *current = (gint)g_ascii_strtoll(current_str, NULL, 10);
        }
        if (maximum) {
            *maximum = (gint)g_ascii_strtoll(max_str, NULL, 10);
        }
        g_free(current_str);
        g_free(max_str);
    }
    g_match_info_free(match_info);
    g_regex_unref(regex);

    if (!matched) {
        g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_INVALID_DATA,
                    "Unable to parse VCP response from ddcutil.");
        return FALSE;
    }

    return TRUE;
}

gboolean ddcutil_get_vcp_value(const gchar *display_id,
                               guint8 code,
                               gint *current,
                               gint *maximum,
                               GError **error) {
    gchar *code_str = g_strdup_printf("%02X", code);
    const gchar *argv[] = {"ddcutil", "--display", display_id, "getvcp", code_str, NULL};
    gchar *output = NULL;
    gboolean success = run_ddcutil(argv, &output, error);
    g_free(code_str);
    if (!success) {
        return FALSE;
    }

    gboolean parsed = parse_vcp_response(output, current, maximum, error);
    g_free(output);
    return parsed;
}

gboolean ddcutil_get_multiple_vcp_values(const gchar *display_id,
                                        const guint8 *codes,
                                        guint n_codes,
                                        DdcutilVcpValue *results,
                                        GError **error) {
    if (!display_id || !codes || n_codes == 0 || !results) {
        return TRUE;
    }

    for (guint i = 0; i < n_codes; i++) {
        results[i].success = FALSE;
        results[i].current = 0;
        results[i].maximum = 0;
        results[i].error_message = NULL;
    }

    gchar **argv = g_new0(gchar *, n_codes + 6);
    guint arg_index = 0;
    argv[arg_index++] = (gchar *)"ddcutil";
    argv[arg_index++] = (gchar *)"--display";
    argv[arg_index++] = (gchar *)display_id;
    argv[arg_index++] = (gchar *)"getvcp";

    gchar **code_strings = g_new0(gchar *, n_codes);
    for (guint i = 0; i < n_codes; i++) {
        code_strings[i] = g_strdup_printf("%02X", codes[i]);
        argv[arg_index++] = code_strings[i];
    }
    argv[arg_index] = NULL;

    gchar *output = NULL;
    gboolean success = run_ddcutil((const gchar *const *)argv, &output, error);

    for (guint i = 0; i < n_codes; i++) {
        g_free(code_strings[i]);
    }
    g_free(code_strings);
    g_free(argv);

    if (!success) {
        g_free(output);
        return FALSE;
    }

    GRegex *entry_regex = g_regex_new("VCP code 0x([0-9A-Fa-f]{2}).*?(?=\\nVCP code 0x|\\z)",
                                      G_REGEX_DOTALL | G_REGEX_MULTILINE,
                                      0,
                                      NULL);
    GMatchInfo *entry_info = NULL;
    g_regex_match(entry_regex, output, 0, &entry_info);

    while (g_match_info_matches(entry_info)) {
        gchar *code_str = g_match_info_fetch(entry_info, 1);
        guint code_value = (guint)g_ascii_strtoll(code_str, NULL, 16);
        g_free(code_str);

        gint index = find_code_index(codes, n_codes, (guint8)code_value);
        if (index >= 0) {
            gchar *entry_text = g_match_info_fetch(entry_info, 0);
            gint current = 0;
            gint maximum = 0;
            GError *parse_error = NULL;
            if (parse_vcp_response(entry_text, &current, &maximum, &parse_error)) {
                results[index].success = TRUE;
                results[index].current = current;
                results[index].maximum = maximum;
            } else {
                if (parse_error && parse_error->message) {
                    results[index].error_message = g_strdup(parse_error->message);
                } else {
                    const gchar *colon = strchr(entry_text, ':');
                    if (colon) {
                        colon++;
                        gchar *trimmed = g_strstrip(g_strdup(colon));
                        results[index].error_message = trimmed;
                    }
                }
                if (parse_error) {
                    g_error_free(parse_error);
                }
                if (!results[index].error_message) {
                    results[index].error_message = g_strdup("Control unavailable for this display.");
                }
            }
            g_free(entry_text);
        }

        g_match_info_next(entry_info, NULL);
    }

    g_match_info_free(entry_info);
    g_regex_unref(entry_regex);
    g_free(output);

    for (guint i = 0; i < n_codes; i++) {
        if (!results[i].success && !results[i].error_message) {
            results[i].error_message = g_strdup("Control unavailable for this display.");
        }
    }

    return TRUE;
}

gboolean ddcutil_set_vcp_value(const gchar *display_id, guint8 code, gint value, GError **error) {
    gchar *code_str = g_strdup_printf("%02X", code);
    gchar *value_str = g_strdup_printf("%d", value);
    const gchar *argv[] = {"ddcutil", "--display", display_id, "setvcp", code_str, value_str, NULL};
    gboolean success = run_ddcutil(argv, NULL, error);
    g_free(code_str);
    g_free(value_str);
    return success;
}

gboolean ddcutil_get_brightness(const gchar *display_id, gint *current, gint *maximum, GError **error) {
    return ddcutil_get_vcp_value(display_id, 0x10, current, maximum, error);
}

gboolean ddcutil_set_brightness(const gchar *display_id, gint value, GError **error) {
    return ddcutil_set_vcp_value(display_id, 0x10, value, error);
}

void ddcutil_monitor_free(DdcutilMonitor *monitor) {
    if (!monitor) {
        return;
    }
    g_free(monitor->display_id);
    g_free(monitor->name);
    g_free(monitor->bus);
    g_free(monitor->serial);
    g_free(monitor);
}
