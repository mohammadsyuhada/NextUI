/*
 * WiFi Direct - wpa_cli based WiFi operations for netplay
 * Uses wpa_cli directly, bypassing wifi_daemon for more reliable operation
 */

#include "wifi_direct.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Simple logging - writes to stderr which goes to the log file
#define LOG_error(fmt, ...) fprintf(stderr, "[ERROR] " fmt, ##__VA_ARGS__)

#define WPA_CLI_CMD "wpa_cli -p /etc/wifi/sockets -i wlan0"
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_CONNECT_CHECK_INTERVAL_MS 500
#define WIFI_SCAN_RETRIES 3
#define WIFI_SCAN_DELAY_MS 1500

// Static state for hotspot
static bool hotspot_active = false;
static char hotspot_ssid[WIFI_DIRECT_SSID_MAX] = {0};
static char hotspot_previous_ssid[128] = {0};

// Helper to sleep in milliseconds
static void wifi_sleep_ms(int ms) {
    usleep(ms * 1000);
}

// Forward declaration
static int wifi_getCurrentSSID(char* ssid_out, size_t ssid_size);

//////////////////////////////////
// WiFi Client Functions (wlan0)
//////////////////////////////////

bool WIFI_direct_ensureReady(void)
{
    // Make sure wlan0 is up
    system("ip link set wlan0 up 2>/dev/null");
    wifi_sleep_ms(200);

    // Check if wpa_supplicant is running
    int ret = system("pidof wpa_supplicant > /dev/null 2>&1");
    if (ret != 0) {
        system("rfkill.elf unblock wifi 2>/dev/null");
        system("/etc/init.d/wpa_supplicant start 2>/dev/null &");
        wifi_sleep_ms(1000);
    }

    // Verify wpa_cli can connect
    FILE* fp = popen(WPA_CLI_CMD " status 2>/dev/null", "r");
    if (!fp) {
        LOG_error("WIFI_direct_ensureReady: wpa_cli failed\n");
        return false;
    }

    char line[128];
    bool ready = false;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "wpa_state=")) {
            ready = true;
            break;
        }
    }
    pclose(fp);

    return ready;
}

// Trigger a WiFi scan (non-blocking, just starts the scan)
void WIFI_direct_triggerScan(void)
{
    system(WPA_CLI_CMD " scan >/dev/null 2>&1");
}

int WIFI_direct_scanNetworks(WIFI_direct_network_t* networks, int max_count)
{
    if (!networks || max_count <= 0) {
        return 0;
    }

    // Note: We don't trigger scan here anymore - caller should call WIFI_direct_triggerScan()
    // and wait before calling this function

    // Get scan results
    FILE* fp = popen(WPA_CLI_CMD " scan_results 2>/dev/null", "r");
    if (!fp) {
        LOG_error("WIFI_direct_scanNetworks: failed to get scan results\n");
        return 0;
    }

    // Get list of saved networks for checking credentials
    char saved_ssids[32][WIFI_DIRECT_SSID_MAX];
    int saved_count = 0;

    FILE* fp_saved = popen(WPA_CLI_CMD " list_networks 2>/dev/null", "r");
    if (fp_saved) {
        char line[256];
        while (fgets(line, sizeof(line), fp_saved) && saved_count < 32) {
            int net_id;
            char net_ssid[128];
            if (sscanf(line, "%d\t%127[^\t]", &net_id, net_ssid) >= 2) {
                strncpy(saved_ssids[saved_count], net_ssid, WIFI_DIRECT_SSID_MAX - 1);
                saved_ssids[saved_count][WIFI_DIRECT_SSID_MAX - 1] = '\0';
                saved_count++;
            }
        }
        pclose(fp_saved);
    }

    int count = 0;
    char line[512];

    // Skip header line
    if (!fgets(line, sizeof(line), fp)) {
        pclose(fp);
        return 0;
    }

    while (fgets(line, sizeof(line), fp) && count < max_count) {
        // Parse wpa_cli scan_results format:
        // bssid / frequency / signal level / flags / ssid
        char bssid[18];
        int freq, rssi;
        char flags[128];
        char ssid[128];

        int parsed = sscanf(line, "%17[0-9a-fA-F:]\t%d\t%d\t%127[^\t]\t%127[^\n]",
                           bssid, &freq, &rssi, flags, ssid);

        if (parsed >= 4) {
            // Handle empty SSID (hidden networks)
            if (parsed < 5 || ssid[0] == '\0') {
                continue;  // Skip hidden networks
            }

            // Skip SSIDs that start with null byte or backslash (escaped nulls like \x00)
            if (ssid[0] == '\0' || ssid[0] == '\\') {
                continue;
            }

            // Trim trailing whitespace and non-printable characters from SSID
            size_t len = strlen(ssid);
            while (len > 0 && ((unsigned char)ssid[len-1] <= ' ' || (unsigned char)ssid[len-1] >= 127)) {
                ssid[--len] = '\0';
            }

            // Trim leading non-printable characters
            char* ssid_start = ssid;
            while (*ssid_start && ((unsigned char)*ssid_start < ' ' || (unsigned char)*ssid_start >= 127)) {
                ssid_start++;
                len--;
            }

            if (len == 0 || ssid_start[0] == '\0') continue;

            // Skip if SSID contains only non-printable characters
            bool has_printable = false;
            for (size_t i = 0; i < len; i++) {
                if ((unsigned char)ssid_start[i] >= ' ' && (unsigned char)ssid_start[i] < 127) {
                    has_printable = true;
                    break;
                }
            }
            if (!has_printable) continue;

            // Use trimmed SSID
            if (ssid_start != ssid) {
                memmove(ssid, ssid_start, len + 1);
            }

            // Check if we already have this SSID (avoid duplicates)
            bool duplicate = false;
            for (int i = 0; i < count; i++) {
                if (strcmp(networks[i].ssid, ssid) == 0) {
                    // Keep the one with stronger signal
                    if (rssi > networks[i].rssi) {
                        networks[i].rssi = rssi;
                    }
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;

            // Fill network info
            strncpy(networks[count].ssid, ssid, WIFI_DIRECT_SSID_MAX - 1);
            networks[count].ssid[WIFI_DIRECT_SSID_MAX - 1] = '\0';
            networks[count].rssi = rssi;
            networks[count].is_secured = (strstr(flags, "WPA") != NULL ||
                                          strstr(flags, "WEP") != NULL ||
                                          strstr(flags, "RSN") != NULL);

            // Check if we have saved credentials
            networks[count].has_saved_creds = false;
            for (int i = 0; i < saved_count; i++) {
                if (strcmp(saved_ssids[i], ssid) == 0) {
                    networks[count].has_saved_creds = true;
                    break;
                }
            }

            count++;
        }
    }
    pclose(fp);

    return count;
}

int WIFI_direct_getCurrentSSID(char* ssid_out, size_t ssid_size) {
    return wifi_getCurrentSSID(ssid_out, ssid_size);
}

bool WIFI_direct_isConnected(void)
{
    FILE* fp = popen(WPA_CLI_CMD " status 2>/dev/null | grep wpa_state", "r");
    if (!fp) {
        return false;
    }

    char line[128];
    bool connected = false;
    if (fgets(line, sizeof(line), fp)) {
        connected = (strstr(line, "COMPLETED") != NULL);
    }
    pclose(fp);

    return connected;
}

// Get current SSID from wpa_cli (internal helper)
static int wifi_getCurrentSSID(char* ssid_out, size_t ssid_size) {
    if (!ssid_out || ssid_size < 1) {
        return -1;
    }
    ssid_out[0] = '\0';

    FILE* fp = popen(WPA_CLI_CMD " status 2>/dev/null | grep '^ssid='", "r");
    if (!fp) {
        return -1;
    }

    char line[128];
    if (fgets(line, sizeof(line), fp)) {
        // Parse "ssid=NetworkName"
        char* eq = strchr(line, '=');
        if (eq) {
            eq++; // Skip '='
            // Trim newline
            size_t len = strlen(eq);
            while (len > 0 && (eq[len-1] == '\n' || eq[len-1] == '\r')) {
                eq[--len] = '\0';
            }
            strncpy(ssid_out, eq, ssid_size - 1);
            ssid_out[ssid_size - 1] = '\0';
            pclose(fp);
            return 0;
        }
    }
    pclose(fp);
    return -1;
}

void WIFI_direct_saveCurrentConnection(void) {
    if (WIFI_direct_isConnected()) {
        char ssid[128];
        if (wifi_getCurrentSSID(ssid, sizeof(ssid)) == 0 && ssid[0] != '\0') {
            strncpy(hotspot_previous_ssid, ssid, sizeof(hotspot_previous_ssid) - 1);
            hotspot_previous_ssid[sizeof(hotspot_previous_ssid) - 1] = '\0';
        }
    }
}

int WIFI_direct_connect(const char* ssid, const char* pass)
{
    if (!ssid) {
        return -1;
    }

    char cmd[512];
    int net_id = -1;
    bool created_new = false;

    // Check if we have a saved network with this SSID
    FILE* fp = popen(WPA_CLI_CMD " list_networks 2>/dev/null", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            int id;
            char net_ssid[128];
            if (sscanf(line, "%d\t%127[^\t]", &id, net_ssid) >= 2) {
                if (strcmp(net_ssid, ssid) == 0) {
                    if (pass == NULL) {
                        // Use saved credentials - just remember this network ID
                        net_id = id;
                    } else {
                        // New password provided - remove old entry
                        snprintf(cmd, sizeof(cmd), WPA_CLI_CMD " remove_network %d >/dev/null 2>&1", id);
                        system(cmd);
                    }
                    break;
                }
            }
        }
        pclose(fp);
    }

    // If no saved network found OR new password provided, create new network entry
    if (net_id < 0) {
        fp = popen(WPA_CLI_CMD " add_network 2>/dev/null", "r");
        if (!fp) {
            LOG_error("WIFI_direct_connect: failed to add network\n");
            return -1;
        }
        char result[32];
        if (fgets(result, sizeof(result), fp)) {
            net_id = atoi(result);
        }
        pclose(fp);

        if (net_id < 0) {
            LOG_error("WIFI_direct_connect: failed to get network id\n");
            return -1;
        }

        created_new = true;

        // Set SSID
        snprintf(cmd, sizeof(cmd), WPA_CLI_CMD " set_network %d ssid '\"%s\"' >/dev/null 2>&1", net_id, ssid);
        system(cmd);

        // Set password or open network
        if (pass && strlen(pass) > 0) {
            snprintf(cmd, sizeof(cmd), WPA_CLI_CMD " set_network %d psk '\"%s\"' >/dev/null 2>&1", net_id, pass);
            system(cmd);
        } else {
            // Open network (no password)
            snprintf(cmd, sizeof(cmd), WPA_CLI_CMD " set_network %d key_mgmt NONE >/dev/null 2>&1", net_id);
            system(cmd);
        }
    }

    // Enable and select the network
    snprintf(cmd, sizeof(cmd), WPA_CLI_CMD " select_network %d >/dev/null 2>&1", net_id);
    system(cmd);

    // Wait for connection (with timeout)
    int elapsed = 0;
    while (elapsed < WIFI_CONNECT_TIMEOUT_MS) {
        wifi_sleep_ms(WIFI_CONNECT_CHECK_INTERVAL_MS);
        elapsed += WIFI_CONNECT_CHECK_INTERVAL_MS;

        if (WIFI_direct_isConnected()) {
            // Request DHCP with persistent background renewal (-b instead of -q)
            system("killall udhcpc 2>/dev/null; udhcpc -i wlan0 -b -t 5 >/dev/null 2>&1 &");

            // Poll for valid IP instead of fixed delay (up to 10 seconds)
            char ip[16] = {0};
            bool got_ip = false;
            for (int i = 0; i < 20; i++) {  // 20 * 500ms = 10 seconds max
                wifi_sleep_ms(500);
                if (WIFI_direct_getIP(ip, sizeof(ip)) == 0 &&
                    ip[0] != '\0' && strcmp(ip, "0.0.0.0") != 0) {
                    got_ip = true;
                    break;
                }
            }

            if (!got_ip) {
                LOG_error("WIFI_direct_connect: DHCP timeout - no IP assigned\n");
            }

            return 0;
        }
    }

    LOG_error("WIFI_direct_connect: connection timeout\n");
    // Clean up on failure (only if we created a new entry)
    if (created_new) {
        snprintf(cmd, sizeof(cmd), WPA_CLI_CMD " remove_network %d >/dev/null 2>&1", net_id);
        system(cmd);
    }

    return -1;
}

void WIFI_direct_disconnect(void)
{
    system(WPA_CLI_CMD " disconnect >/dev/null 2>&1");
}

void WIFI_direct_forget(const char* ssid)
{
    if (!ssid || !ssid[0]) {
        return;
    }

    // Find network ID by SSID
    FILE* fp = popen(WPA_CLI_CMD " list_networks 2>/dev/null", "r");
    if (!fp) {
        LOG_error("WIFI_direct_forget: failed to list networks\n");
        return;
    }

    int network_id = -1;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        int id;
        char net_ssid[128];
        // Format: "network_id / ssid / bssid / flags"
        if (sscanf(line, "%d\t%127[^\t]", &id, net_ssid) == 2) {
            if (strcmp(net_ssid, ssid) == 0) {
                network_id = id;
                break;
            }
        }
    }
    pclose(fp);

    if (network_id >= 0) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), WPA_CLI_CMD " remove_network %d >/dev/null 2>&1", network_id);
        system(cmd);
        system(WPA_CLI_CMD " save_config >/dev/null 2>&1");
    }
}

int WIFI_direct_scanForHotspots(const char* prefix, char ssids_out[][WIFI_DIRECT_SSID_MAX], int max_count)
{
    if (!prefix || !ssids_out || max_count <= 0) {
        return 0;
    }

    int count = 0;
    size_t prefix_len = strlen(prefix);

    // Try multiple scans to increase chance of finding hotspots
    for (int retry = 0; retry < WIFI_SCAN_RETRIES && count == 0; retry++) {
        // Trigger scan
        system(WPA_CLI_CMD " scan >/dev/null 2>&1");
        wifi_sleep_ms(WIFI_SCAN_DELAY_MS);

        // Get scan results
        FILE* fp = popen(WPA_CLI_CMD " scan_results 2>/dev/null", "r");
        if (!fp) {
            LOG_error("WIFI_direct_scanForHotspots: failed to get scan results\n");
            continue;
        }

        char line[512];
        while (fgets(line, sizeof(line), fp) && count < max_count) {
            // Parse wpa_cli scan_results format:
            // bssid / frequency / signal level / flags / ssid
            char bssid[18];
            int freq, rssi;
            char flags[128];
            char ssid[128];

            int parsed = sscanf(line, "%17[0-9a-fA-F:]\t%d\t%d\t%127[^\t]\t%127[^\n]",
                               bssid, &freq, &rssi, flags, ssid);

            if (parsed >= 5 && ssid[0] != '\0') {
                // Trim trailing whitespace
                size_t len = strlen(ssid);
                while (len > 0 && (ssid[len-1] == ' ' || ssid[len-1] == '\n' || ssid[len-1] == '\r')) {
                    ssid[--len] = '\0';
                }

                // Check if SSID matches prefix
                if (strncmp(ssid, prefix, prefix_len) == 0) {
                    strncpy(ssids_out[count], ssid, WIFI_DIRECT_SSID_MAX - 1);
                    ssids_out[count][WIFI_DIRECT_SSID_MAX - 1] = '\0';
                    count++;
                }
            }
        }
        pclose(fp);
    }

    return count;
}

int WIFI_direct_getIP(char* ip_out, size_t ip_size)
{
    if (!ip_out || ip_size < 16) {
        return -1;
    }

    ip_out[0] = '\0';

    // Get IP address using ifconfig (BusyBox compatible)
    FILE* fp = popen("ifconfig wlan0 2>/dev/null | grep 'inet addr' | sed 's/.*inet addr:\\([0-9.]*\\).*/\\1/'", "r");
    if (!fp) {
        return -1;
    }

    char line[64];
    if (fgets(line, sizeof(line), fp)) {
        // Trim newline
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }

        if (len > 0) {
            strncpy(ip_out, line, ip_size - 1);
            ip_out[ip_size - 1] = '\0';
            pclose(fp);
            return 0;
        }
    }
    pclose(fp);

    return -1;
}

bool WIFI_direct_restorePreviousConnection(void) {
    if (hotspot_previous_ssid[0] == '\0') {
        return false;
    }

    // Try to find and enable a saved network with this SSID
    char cmd[256];
    FILE* fp = popen(WPA_CLI_CMD " list_networks 2>/dev/null", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            int net_id;
            char net_ssid[128];
            if (sscanf(line, "%d\t%127[^\t]", &net_id, net_ssid) >= 2) {
                if (strcmp(net_ssid, hotspot_previous_ssid) == 0) {
                    pclose(fp);

                    // Select and enable this network
                    snprintf(cmd, sizeof(cmd), WPA_CLI_CMD " select_network %d >/dev/null 2>&1", net_id);
                    system(cmd);

                    // Wait for connection
                    for (int i = 0; i < 20; i++) {
                        wifi_sleep_ms(500);
                        if (WIFI_direct_isConnected()) {
                            char current_ssid[128];
                            if (wifi_getCurrentSSID(current_ssid, sizeof(current_ssid)) == 0) {
                                if (strcmp(current_ssid, hotspot_previous_ssid) == 0) {
                                    // Request DHCP to get IP address
                                    system("killall udhcpc 2>/dev/null; udhcpc -i wlan0 -b -t 5 >/dev/null 2>&1 &");
                                    hotspot_previous_ssid[0] = '\0';
                                    return true;
                                }
                            }
                        }
                    }
                    hotspot_previous_ssid[0] = '\0';
                    return false;
                }
            }
        }
        pclose(fp);
    }

    // Network not in saved list, try to use wpa_supplicant.conf
    system(WPA_CLI_CMD " reconnect >/dev/null 2>&1");

    // Wait for connection
    for (int i = 0; i < 20; i++) {
        wifi_sleep_ms(500);
        if (WIFI_direct_isConnected()) {
            // Request DHCP to get IP address
            system("killall udhcpc 2>/dev/null; udhcpc -i wlan0 -b -t 5 >/dev/null 2>&1 &");
            hotspot_previous_ssid[0] = '\0';
            return true;
        }
    }

    hotspot_previous_ssid[0] = '\0';
    return false;
}

//////////////////////////////////
// Hotspot Functions (wlan1 AP Mode)
//////////////////////////////////

int WIFI_direct_startHotspot(const char* ssid, const char* password) {
    if (hotspot_active) {
        return 0;
    }

    // Save current WiFi connection before disconnecting
    WIFI_direct_saveCurrentConnection();

    // Clean up any leftover state from a previous hotspot that wasn't properly stopped
    system("killall hostapd 2>/dev/null");
    system("killall udhcpd 2>/dev/null");
    system("ip addr flush dev wlan1 2>/dev/null");
    system("ip link set wlan1 down 2>/dev/null");
    wifi_sleep_ms(200);

    // Disconnect and disable wlan0 to free up resources for AP mode
    if (WIFI_direct_isConnected()) {
        WIFI_direct_disconnect();
        wifi_sleep_ms(300);
    }
    // IMPORTANT: Flush wlan0 IP to avoid conflict with hotspot subnet (10.0.0.x)
    // If wlan0 had an IP like 10.0.0.10 from a previous client session, it will
    // conflict with the DHCP range of the hotspot we're about to start
    system("ip addr flush dev wlan0 2>/dev/null");
    system("ip link set wlan0 down 2>/dev/null");
    wifi_sleep_ms(200);

    // Create hostapd config
    FILE* f = fopen("/tmp/gbalink_hostapd.conf", "w");
    if (!f) {
        LOG_error("WIFI_direct_startHotspot: failed to create hostapd config\n");
        return -1;
    }
    fprintf(f, "interface=wlan1\n");
    fprintf(f, "driver=nl80211\n");
    fprintf(f, "ssid=%s\n", ssid);
    fprintf(f, "channel=6\n");
    fprintf(f, "hw_mode=g\n");
    fprintf(f, "auth_algs=1\n");
    fprintf(f, "wpa=2\n");
    fprintf(f, "wpa_passphrase=%s\n", password);
    fprintf(f, "wpa_key_mgmt=WPA-PSK\n");
    fprintf(f, "rsn_pairwise=CCMP\n");
    fclose(f);

    // Create udhcpd config
    f = fopen("/tmp/gbalink_udhcpd.conf", "w");
    if (!f) {
        LOG_error("WIFI_direct_startHotspot: failed to create udhcpd config\n");
        return -1;
    }
    fprintf(f, "start 10.0.0.10\n");
    fprintf(f, "end 10.0.0.50\n");
    fprintf(f, "interface wlan1\n");
    fprintf(f, "pidfile /tmp/gbalink_udhcpd.pid\n");
    fprintf(f, "lease_file /tmp/gbalink_udhcpd.leases\n");
    fprintf(f, "option subnet 255.255.255.0\n");
    fprintf(f, "option router 10.0.0.1\n");
    fclose(f);

    // Bring up wlan1 interface
    int ret = system("ip link set wlan1 up");
    if (ret != 0) {
        LOG_error("WIFI_direct_startHotspot: failed to bring up wlan1\n");
        return -1;
    }

    // Set static IP on wlan1
    ret = system("ip addr add 10.0.0.1/24 dev wlan1 2>/dev/null");
    // Ignore error if address already exists

    // Start hostapd
    ret = system("hostapd -B /tmp/gbalink_hostapd.conf");
    if (ret != 0) {
        LOG_error("WIFI_direct_startHotspot: failed to start hostapd\n");
        system("ip addr del 10.0.0.1/24 dev wlan1 2>/dev/null");
        system("ip link set wlan1 down");
        return -1;
    }

    // Start DHCP server
    ret = system("udhcpd /tmp/gbalink_udhcpd.conf");
    if (ret != 0) {
        LOG_error("WIFI_direct_startHotspot: failed to start udhcpd\n");
        system("killall hostapd 2>/dev/null");
        system("ip addr del 10.0.0.1/24 dev wlan1 2>/dev/null");
        system("ip link set wlan1 down");
        return -1;
    }

    strncpy(hotspot_ssid, ssid, sizeof(hotspot_ssid) - 1);
    hotspot_ssid[sizeof(hotspot_ssid) - 1] = '\0';
    hotspot_active = true;
    return 0;
}

int WIFI_direct_stopHotspot(void) {
    if (!hotspot_active) {
        return 0;
    }

    // Kill hostapd first and wait for it to fully terminate
    system("killall hostapd 2>/dev/null");
    wifi_sleep_ms(200);  // Wait for hostapd to release the interface

    // Kill udhcpd using pidfile
    system("kill $(cat /tmp/gbalink_udhcpd.pid 2>/dev/null) 2>/dev/null");

    // Bring down wlan1 interface completely
    system("ip addr flush dev wlan1 2>/dev/null");
    system("ip link set wlan1 down");

    // Cleanup temp files
    system("rm -f /tmp/gbalink_*.conf /tmp/gbalink_*.pid /tmp/gbalink_*.leases 2>/dev/null");

    hotspot_active = false;
    hotspot_ssid[0] = '\0';
    // NOTE: Don't clear hotspot_previous_ssid here - it's needed by
    // WIFI_direct_restorePreviousConnection() which is called later

    // Reset wlan0 to clear any bad state
    system("ip link set wlan0 down 2>/dev/null");
    wifi_sleep_ms(100);
    system("ip link set wlan0 up 2>/dev/null");
    wifi_sleep_ms(200);

    // Force wpa_supplicant to reload its configuration
    system(WPA_CLI_CMD " reconfigure >/dev/null 2>&1");
    wifi_sleep_ms(500);

    return 0;
}

bool WIFI_direct_isHotspotActive(void) {
    return hotspot_active;
}

const char* WIFI_direct_getHotspotIP(void) {
    return WIFI_DIRECT_HOTSPOT_IP;
}

const char* WIFI_direct_getHotspotSSID(void) {
    return hotspot_ssid;
}

const char* WIFI_direct_getHotspotSSIDPrefix(void) {
    return LINK_HOTSPOT_SSID_PREFIX;
}

const char* WIFI_direct_getHotspotPassword(void) {
    return WIFI_DIRECT_HOTSPOT_PASS;
}
