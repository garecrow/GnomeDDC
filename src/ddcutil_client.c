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
                    "Unable to parse response from ddcutil.");
        return FALSE;
    }

    return TRUE;
}

gboolean ddcutil_get_vcp_value(const gchar *display_id,
                               const gchar *vcp_code,
                               gint *current,
                               gint *maximum,
                               GError **error) {
    const gchar *argv[] = {"ddcutil", "--display", display_id, "getvcp", vcp_code, NULL};
    gchar *output = NULL;
    gboolean success = run_ddcutil(argv, &output, error);
    if (!success) {
        return FALSE;
    }

    gboolean parsed = parse_vcp_response(output, current, maximum, error);
    g_free(output);
    return parsed;
}

gboolean ddcutil_get_brightness(const gchar *display_id, gint *current, gint *maximum, GError **error) {
    return ddcutil_get_vcp_value(display_id, "10", current, maximum, error);
}

gboolean ddcutil_get_multiple_vcp_values(const gchar *display_id,
                                         const gchar *const *vcp_codes,
                                         guint n_codes,
                                         DdcutilVcpValue **out_values,
                                         GError **error) {
    g_return_val_if_fail(display_id != NULL, FALSE);
    g_return_val_if_fail(out_values != NULL, FALSE);

    if (n_codes == 0) {
        *out_values = NULL;
        return TRUE;
    }

    g_auto(GStrv) argv = g_new0(gchar *, 5 + n_codes);
    guint argc = 0;
    argv[argc++] = g_strdup("ddcutil");
    argv[argc++] = g_strdup("--display");
    argv[argc++] = g_strdup(display_id);
    argv[argc++] = g_strdup("getvcp");
    for (guint i = 0; i < n_codes; i++) {
        argv[argc++] = g_strdup(vcp_codes[i]);
    }

    gchar *output = NULL;
    if (!run_ddcutil((const gchar *const *)argv, &output, error)) {
        return FALSE;
    }

    DdcutilVcpValue *values = g_new0(DdcutilVcpValue, n_codes);
    for (guint i = 0; i < n_codes; i++) {
        if (vcp_codes[i]) {
            g_strlcpy(values[i].code, vcp_codes[i], sizeof(values[i].code));
        }
        values[i].maximum = 100;
    }

    GRegex *regex = g_regex_new("VCP code 0x([0-9A-Fa-f]{2}).*?current value\\s*=\\s*(\\d+).*?max value\\s*=\\s*(\\d+)",
                                 G_REGEX_DOTALL,
                                 0,
                                 NULL);
    GMatchInfo *match_info = NULL;
    g_regex_match(regex, output, 0, &match_info);

    gboolean any_matched = FALSE;

    while (g_match_info_matches(match_info)) {
        gchar *code_str = g_match_info_fetch(match_info, 1);
        gchar *current_str = g_match_info_fetch(match_info, 2);
        gchar *max_str = g_match_info_fetch(match_info, 3);

        guint parsed_code = (guint)g_ascii_strtoull(code_str, NULL, 16);
        for (guint i = 0; i < n_codes; i++) {
            guint requested_code = (guint)g_ascii_strtoull(values[i].code, NULL, 16);
            if (parsed_code == requested_code) {
                values[i].available = TRUE;
                values[i].current = (gint)g_ascii_strtoll(current_str, NULL, 10);
                values[i].maximum = (gint)g_ascii_strtoll(max_str, NULL, 10);
                any_matched = TRUE;
                break;
            }
        }

        g_free(code_str);
        g_free(current_str);
        g_free(max_str);
        g_match_info_next(match_info, NULL);
    }

    g_match_info_free(match_info);
    g_regex_unref(regex);
    g_free(output);

    if (!any_matched) {
        g_free(values);
        g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_INVALID_DATA,
                    "ddcutil did not return any VCP values.");
        return FALSE;
    }

    *out_values = values;
    return TRUE;
}

gboolean ddcutil_set_vcp_value(const gchar *display_id, const gchar *vcp_code, gint value, GError **error) {
    gchar *value_str = g_strdup_printf("%d", value);
    const gchar *argv[] = {"ddcutil", "--display", display_id, "setvcp", vcp_code, value_str, NULL};
    gboolean success = run_ddcutil(argv, NULL, error);
    g_free(value_str);
    return success;
}

gboolean ddcutil_set_brightness(const gchar *display_id, gint value, GError **error) {
    return ddcutil_set_vcp_value(display_id, "10", value, error);
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
