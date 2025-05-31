#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <switch.h>
#include <curl/curl.h>
#include <pthread.h>
#include <errno.h>
#include <stdatomic.h>
#include "dashboard.h"
#include "switch/crypto/sha1.h"
#include "jsmn.h"

#define CONFIG_FILE "config.txt"                            ///< config file/path
#define SOFTWARE "nxducominer"                              ///< software identifier
#define GET_POOL "https://server.duinocoin.com/getPool"     ///< pool server url


#define DUCO_ORANGE CONSOLE_ESC(38;2;252;104;3m)            ///< duinocoin branding https://github.com/revoxhere/duino-coin/tree/useful-tools#branding
#define ERROR_RED CONSOLE_ESC(31m)
#define NOTICE_BLUE CONSOLE_ESC(38;2;135;206;235m)
#define DARK_GREY CONSOLE_ESC(38;2;90;90;90m)
#define LIGHT_GREY CONSOLE_ESC(38;2;135;135;135m)
#define RESET CONSOLE_ESC(0m)

#define CONSOLE_ESC_NSTR(fmt) "\033[" fmt                   ///< console escape helper, does not stringify.

/**
 * @defgroup structures Data Structures
 * @{
*/

/**
 * @brief aggregated mining results of all threads
*/
typedef struct {
    float total_hashrate;                                   ///< combined hashrate (kH/s)
    int total_difficulty;                                   ///< combined difficulty
    float avg_difficulty;                                   ///< average difficulty
    int total_shares;                                       ///< shares submitted
    int good_shares;                                        ///< shares accepted
    int bad_shares;                                         ///< shares rejected
    int blocks;                                             ///< blocks found
} MiningResults;

MiningResults mr = {
    .total_hashrate = 0.0f,
    .total_difficulty = 0,
    .avg_difficulty = 0.0f,
    .total_shares = 0,
    .good_shares = 0,
    .bad_shares = 0,
    .blocks = 0
};

/**
 * @brief thread-specific mining data and state
 */
typedef struct {
    int socket_fd;
    int thread_id;
    float hashrate;
    int difficulty;
    int total_shares;
    int good_shares;
    int bad_shares;
    int blocks;
    char* error; 
    sig_atomic_t stop_mining;

} ThreadData;

/**
 * @brief web dashboard server information
 */
typedef struct {
    int server_fd;
    int client_fd;
    pthread_t wd_thread;
} WebDashboard;

WebDashboard web = {
    .server_fd = -1,
    .client_fd = -1,
    .wd_thread = NULL
};

/**
 * @brief resource manager for system-wide state
 */
typedef struct {
    int last_share;                                         ///< last nonce value submitted
    u32 charge;                                             ///< raw charge
    char* chargeType;                                       ///< charger type string
    s32 skin_temp_milli_c;                                  ///< temp in milliC
    pthread_mutex_t lock;                                   ///< UNUSED
    pthread_t* mining_threads;                              ///< array of mining threads
    ThreadData* thread_data;                                ///< array of thread-specific data
    int single_miner_id;                                    ///< single id for all threads, to display as a single device in wallet
    WebDashboard* web_dashboard;
} ResourceManager;

ResourceManager res = {
   .last_share = 0,
   .charge = 0,
   .chargeType = 0,
   .skin_temp_milli_c = 0,
   .lock = PTHREAD_MUTEX_INITIALIZER,
   .mining_threads = NULL,
   .thread_data = NULL,
   .single_miner_id = 0,
   .web_dashboard = &web
};

/**
 * @brief mining configuration from config file
 */
typedef struct {
    char* node;
    int port;
    char* wallet_address;
    char* miner_key;
    char* difficulty;
    char* rig_id;
    bool cpu_boost;
    bool iot;
    int threads;
    bool web_dashboard;
} MiningConfig;

MiningConfig mc = {
   .node = NULL,
   .wallet_address = NULL,
   .miner_key = NULL,
   .difficulty = NULL,
   .rig_id = NULL,
   .port = 0,
   .cpu_boost = false,
   .iot = false,
   .threads = 1,
   .web_dashboard = false
};

/**
 * @brief memory buffer for CURL operation
 */
struct MemoryStruct {
    char* memory;                                           ///< ptr to allocated memory
    size_t size;                                            ///< size of allocated memory
};
/** @} **/

/**
 * @brief clean up resources and exit
 * @param msg optional error message to display before exiting
 * @note This function terminates the program
 */
void cleanup(char* msg) {
    printf(CONSOLE_ESC(80;1H));     //move cursor
    printf(CONSOLE_ESC(2K));        //clear line

    if (msg == NULL) {
        printf(CONSOLE_ESC(80;1H) "Exiting...");
        consoleUpdate(NULL);
    }
    else {
        printf(ERROR_RED);
        printf(CONSOLE_ESC(80;1H) "ERROR: %s. Exiting...", msg);
        printf(RESET);
        consoleUpdate(NULL);
    }
    sleep(3);

    //cleanup threads
    if (res.mining_threads && res.thread_data) {
        for (int i = 0; i < mc.threads; i++) {
            if (res.thread_data[i].socket_fd >= 0) {
                close(res.thread_data[i].socket_fd);                //close sockets
                res.thread_data[i].socket_fd = -1;
            }
            res.thread_data[i].stop_mining = 1;                     //set atomic signal

            if (res.mining_threads[i]) {
                pthread_cancel(res.mining_threads[i]);              //cancel threads
            }
        }

        for (int i = 0; i < mc.threads; i++) {
            if (res.mining_threads[i]) {
                pthread_join(res.mining_threads[i], NULL);          //join threads
            }
        }
    }

    //cleanup web dashboard
    if (mc.web_dashboard && res.web_dashboard->wd_thread != NULL) {
        close(res.web_dashboard->client_fd);
        close(res.web_dashboard->server_fd);
        pthread_cancel(res.web_dashboard->wd_thread);
        pthread_join(res.web_dashboard->wd_thread, NULL);
        free(res.web_dashboard);
    }

    // free resources
    if (mc.difficulty) free(mc.difficulty);
    if (mc.miner_key) free(mc.miner_key);
    if (mc.node) free(mc.node);
    if (mc.rig_id) free(mc.rig_id);
    if (mc.wallet_address) free(mc.wallet_address);
    
    psmExit();
    tcExit();
    socketExit();
    consoleExit(NULL);
    exit(0);
}

/**
 * @defgroup utils Utility Functions
 * @{
 */

 /**
  * @brief string representation of charger type
  * @param type charger type enum value
  * @return human readable charger state string
  */
const char* get_psm_charger_type(PsmChargerType type) {
    switch (type) {
    case PsmChargerType_Unconnected:   return "discharging";
    case PsmChargerType_EnoughPower:   return "charging";
    case PsmChargerType_LowPower:      return "charging slowly";
    case PsmChargerType_NotSupported:  return "plugged in, not charging";
    default:                           return "unknown charger state";
    }
}

/**
 * @brief get current time as formatted string
 * @param buffer output buffer for time string
 * @param size size of output buffer
 */
void get_time_string(char* buffer, int size) {
    time_t raw_time = time(NULL);
    struct tm* time_info = localtime(&raw_time);
    strftime(buffer, size, "%H:%M:%S", time_info);
}

/**
 * @brief safe malloc with zero-initialization
 * @param size number of bytes to allocate
 * @return ptr to allocated memory
 * @note calls cleanup() on allocation failure
 */
void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        cleanup("memory allocation failed");
    }
    memset(ptr, 0, size);
    return ptr;
}

/**
 * @brief safe strdup
 * @param src source string to duplicate
 * @return new string copy
 * @note calls cleanup() on allocation failure
 */
char* safe_strdup(const char* src) {
    if (!src) return NULL;
    char* dst = safe_malloc(strlen(src) + 1);
    strcpy(dst, src);
    return dst;
}

/**
 * @brief set dynamic string field with memory management
 * @param field pointer to string to update
 * @param value new string value
 * @note calls cleanup() on allocation failure
 */
void set_dynamic_string(char** field, const char* value) {
    free(*field);
    *field = safe_strdup(value);
    if (*field == NULL) {
        cleanup("memory allocation failed");
    }
}

/**
 * @brief safe socket write with retry
 * @param fd fd to write to
 * @param buf buffer to write
 * @param len length of data to write
 * @return total bytes written, or -1 on error
 */
ssize_t safe_write(int fd, const char* buf, size_t len) {
    if (fd < 0) return -1;

    ssize_t total = 0;
    while (total < len) {
        ssize_t sent = write(fd, buf + total, len - total);
        if (sent <= 0) return -1;  //error
        total += sent;
    }
    return total;
}

/**
 * @brief CURL write callback for memory buffer
 * @param contents received data
 * @param size size of data
 * @param nmemb number of elements
 * @param userp ptr to MemoryStruct
 * @return total size of processed data
 */
static size_t write_memory_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct* mem = (struct MemoryStruct*)userp;

    char* ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        free(mem->memory);
        mem->memory = NULL;
        mem->size = 0;
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

/**
 * @brief replace all occurrences of placeholder in string (html) with value
 * @param str ptr to the string pointer that contains placeholders to replace.
 * @param placeholder the placeholder text to search for in the string (e.g., "@@HASHRATE@@").
 * @param value the replacement text to substitute for the placeholder.
 */
void replace_placeholder(char** str, const char* placeholder, const char* value) {
    if (!str || !*str || !placeholder || !value) return;

    char* current = *str;
    size_t placeholder_len = strlen(placeholder);
    size_t value_len = strlen(value);

    size_t new_len = strlen(current) + 1;
    char* result = malloc(new_len);
    strcpy(result, current);

    char* pos;
    while ((pos = strstr(result, placeholder)) != NULL) {
        size_t prefix_len = pos - result;
        size_t suffix_len = strlen(pos + placeholder_len);
        new_len = prefix_len + value_len + suffix_len + 1;

        char* new_result = realloc(result, new_len);
        if (!new_result) {
            free(result);
            return;
        }
        result = new_result;
        pos = result + prefix_len;

        memmove(pos + value_len, pos + placeholder_len, suffix_len + 1);
        memcpy(pos, value, value_len);
    }

    free(*str);
    *str = result;
}
/** @} **/

/**
 * @brief parse configuration file
 * @param config populates MiningConfig
 * @note calls cleanup() on error
 */
void parse_config_file(MiningConfig* config) {
    printf("Reading %s\n", CONFIG_FILE);
    FILE* file = fopen(CONFIG_FILE, "r");
    if (file == NULL) {
        cleanup("Failed to open config file");
    }

    char line[100];
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        char* sep = strchr(line, ':');
        if (!sep) continue;

        *sep = '\0';
        char* key = line;
        char* value = sep + 1;

        while (*value == ' ') value++;

        printf("%s:%s\n", key, value);
        consoleUpdate(NULL);

        if (strcmp(key, "node") == 0) {
            set_dynamic_string(&config->node, value);
        }
        else if (strcmp(key, "port") == 0) {
            config->port = atoi(value);
        }
        else if (strcmp(key, "wallet_address") == 0) {
            if (strlen(value) < 1)
                cleanup("wallet_address not set");
            set_dynamic_string(&config->wallet_address, value);
        }
        else if (strcmp(key, "miner_key") == 0) {
            set_dynamic_string(&config->miner_key, value);
        }
        else if (strcmp(key, "difficulty") == 0) {
            if (strlen(value) < 1)
                cleanup("difficulty not set");
            set_dynamic_string(&config->difficulty, value);
        }
        else if (strcmp(key, "rig_id") == 0) {
            if (strlen(value) < 1)
                cleanup("rig_id not set");
            set_dynamic_string(&config->rig_id, value);
        }
        else if (strcmp(key, "cpu_boost") == 0) {
            if (strlen(value) < 1)
                cleanup("cpu_boost not set");
            config->cpu_boost = (strcmp(value, "true") == 0) ? true : false;
        }
        else if (strcmp(key, "iot") == 0) {
            if (strlen(value) < 1)
                cleanup("iot not set");
            config->iot = (strcmp(value, "true") == 0) ? true : false;
        }
        else if (strcmp(key, "threads") == 0) {
            config->threads = atoi(value);
            if (config->threads < 1 || config->threads > 6)
                cleanup("threads value out of range");
        }
        else if (strcmp(key, "web_dashboard") == 0) {
            if (strlen(value) < 1)
                cleanup("web_dashboard not set");
            config->web_dashboard = (strcmp(value, "true") == 0) ? true : false;
        }
    }
    fclose(file);
    printf("File parsing completed");
}


/**
 * @brief get mining node from DuinoCoin server
 * @param ip output parameter for node IP
 * @param port output parameter for node port
 * @note calls cleanup() on error
 */
void get_node(char** ip, int* port) {
    printf(CONSOLE_ESC(2J));
    printf(CONSOLE_ESC(1;1H) "Finding a node from master server...");
    consoleUpdate(NULL);
    sleep(2);

    CURL* curl;
    CURLcode res;

    char* json_copy = NULL;
    struct MemoryStruct chunk;
    chunk.memory = safe_malloc(1);  // start with empty buffer
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (!curl) {
        cleanup("curl init failed");
    }

    curl_easy_setopt(curl, CURLOPT_URL, GET_POOL);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libnx curl nxducominer");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        cleanup("curl failed");
    }

    curl_easy_cleanup(curl);
    json_copy = safe_strdup(chunk.memory);
    if (!json_copy) {
        curl_easy_cleanup(curl);
        free(chunk.memory);
        cleanup("curl stdrup failed");
    }

    jsmn_parser parser;
    jsmntok_t tokens[13];
    jsmn_init(&parser);

    int ret = jsmn_parse(&parser, json_copy, strlen(json_copy), tokens, 13);
    if (ret < 0) {
        if(json_copy) free(json_copy);
        if(chunk.memory) free(chunk.memory);
        curl_easy_cleanup(curl);
        cleanup("failed to parse JSON");
    }

    for (int i = 1; i < ret; i++) {
        if (tokens[i].type == JSMN_STRING) {
            if (strncmp(json_copy + tokens[i].start, "ip", tokens[i].end - tokens[i].start) == 0) {
                if (i + 1 < ret) {
                    char ip_str[16];
                    int length = tokens[i + 1].end - tokens[i + 1].start;
                    strncpy(ip_str, json_copy + tokens[i + 1].start, length);
                    *ip = safe_malloc(length + 1);
                    strncpy(*ip, json_copy + tokens[i + 1].start, length);
                    (*ip)[length] = '\0';
                    i++;
                }
            }
            else if (strncmp(json_copy + tokens[i].start, "port", tokens[i].end - tokens[i].start) == 0) {
                if (i + 1 < ret) {
                    char port_str[16];
                    int length = tokens[i + 1].end - tokens[i + 1].start;
                    strncpy(port_str, json_copy + tokens[i + 1].start, length);
                    port_str[length] = '\0';
                    *port = atoi(port_str);
                    i++;
                }
            }
        }
    }

    printf(CONSOLE_ESC(3;1H)"Using %s:%i...", mc.node, mc.port);
    consoleUpdate(NULL);
    sleep(2);

    if(chunk.memory) free(chunk.memory);
    if(json_copy) free(json_copy);
}

/**
 * @brief mining thread worker function
 * @param arg ThreadData structure for this thread
 * @return NULL
 */
void* do_mining_work(void* arg) {
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    ThreadData* td = (ThreadData*)arg;
    char recv_buf[1024];

    while (!td->stop_mining) {
        reconnect:
            pthread_testcancel();
            int s = socket(AF_INET, SOCK_STREAM, 0);
            if (s < 0) {
                char errbuf[256];
                snprintf(errbuf, sizeof(errbuf), "Socket creation failed: %s", strerror(errno));
                td->error = safe_strdup(errbuf);
                td->socket_fd = -1;
                sleep(1);
                continue;
            }
            else {
                td->socket_fd = s;
            }

            struct hostent* server = gethostbyname(mc.node);
            if (!server) {
                td->error = safe_strdup("No such host");
                close(td->socket_fd);
                sleep(1);
                continue;
            }

            struct sockaddr_in serv_addr;
            memset(&serv_addr, 0, sizeof(serv_addr));
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(mc.port);
            memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

            if (connect(td->socket_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
                td->error = safe_strdup("Failed to connect to host");
                close(td->socket_fd);
                sleep(1);
                continue;
            }
            read(td->socket_fd, recv_buf, sizeof(4));

            td->error = NULL; //if we got to this point, errors from above are no longer applicable
            while (!td->stop_mining) {
                pthread_testcancel();

                // request job
                char job_request[256];
                if (mc.iot && td->thread_id == 0) { //only send iot information on one thread
                    char iot[64];
                    snprintf(iot, sizeof(iot), "Charge:%u%%@Temp:%.2f*C", res.charge, res.skin_temp_milli_c / 1000.0f);
                    snprintf(job_request, sizeof(job_request),
                        "JOB,%s,%s,%s,%s",
                        mc.wallet_address, mc.difficulty, mc.miner_key, iot);
                }
                else {
                    snprintf(job_request, sizeof(job_request),
                        "JOB,%s,%s,%s",
                        mc.wallet_address, mc.difficulty, mc.miner_key);
                }

                int write_job = safe_write(td->socket_fd, job_request, strlen(job_request));
                if (write_job <= 0) goto reconnect;     //failed to send job request

                // receive job
                memset(recv_buf, 0, 1024);
                int read_job = read(td->socket_fd, recv_buf, 1024 - 1);
                if (read_job <= 0) goto reconnect;      //failed to recieve job

                // split job parts
                char* job_parts[3];
                char* saveptr;
                char* token = strtok_r(recv_buf, ",", &saveptr);
                for (int i = 0; i < 3 && token; i++) {
                    job_parts[i] = token;
                    token = strtok_r(NULL, ",", &saveptr);
                }

                int difficulty = atoi(job_parts[2]);
                char base_str[128];
                char expected_hash[41];
                strcpy(base_str, job_parts[0]);
                strcpy(expected_hash, job_parts[1]);

                // initialize sha1 context
                Sha1Context base_ctx;
                Sha1Context temp_ctx;
                sha1ContextCreate(&base_ctx);
                sha1ContextUpdate(&base_ctx, (const unsigned char*)base_str, strlen(base_str));

                time_t start_time = time(NULL);
                char result_hash[41];
                int nonce;

                for (nonce = 0; nonce <= (100 * difficulty + 1); nonce++) {
                    pthread_testcancel();

                    unsigned char hash[20];
                    char result_str[16];

                    temp_ctx = base_ctx;  // copy base_ctx to temp_ctx
                    int len = sprintf(result_str, "%d", nonce);
                    sha1ContextUpdate(&temp_ctx, (const unsigned char*)result_str, len);
                    sha1ContextGetHash(&temp_ctx, hash);

                    // compare hash
                    for (int i = 0; i < 20; i++) {
                        snprintf(result_hash + (i * 2), 3, "%02x", hash[i]);
                    }

                    if (memcmp(result_hash, expected_hash, 20) == 0) {
                        double elapsed = difftime(time(NULL), start_time);
                        double hashrate = nonce / (elapsed > 0 ? elapsed : 1);

                        // send result
                        char submit_buf[128];
                        int len = snprintf(submit_buf, sizeof(submit_buf), "%d,%.2f,%s,%s,,%i",
                            nonce, hashrate, SOFTWARE, mc.rig_id, res.single_miner_id);
                        int write_result = safe_write(td->socket_fd, submit_buf, len);
                        if (write_result <= 0) goto reconnect;      //failed to send job result
                        // read response
                        int read_result = read(td->socket_fd, recv_buf, 1024 - 1);
                        if(read_result <= 0) goto reconnect;        //failed to recieve job result feedback

                        if (strncmp(recv_buf, "GOOD", 4) == 0) {
                            td->good_shares++;
                        }
                        else if (strncmp(recv_buf, "BLOCK", 5) == 0) {
                            td->blocks++;
                        }
                        else {
                            td->bad_shares++;
                        }

                        res.last_share = nonce;
                        td->difficulty = difficulty;
                        td->hashrate = hashrate / 1000.0f;
                        td->total_shares++;

                        break;
                    }
                    td->error = NULL;
                }
            }
        return NULL;
    }
    return NULL;
}

/**
 * @brief web dashboard server thread
 * @param arg WebDashboard struct
 * @return NULL
 */
void* web_dashboard(void* arg) {
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    struct sockaddr_in address = { 0 };
    int addrlen = sizeof(address);

    if ((res.web_dashboard->server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == 0) {
        cleanup("web dashboard socket failed.");
    }

    int opt = 1;
    setsockopt(res.web_dashboard->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(res.web_dashboard->server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        cleanup("failed to bind");
    }

    if (listen(res.web_dashboard->server_fd, 3) < 0) {
        cleanup("web dashboard socket failed to listen");
    }

    while (1) {
        pthread_testcancel();

        res.web_dashboard->client_fd = accept(res.web_dashboard->server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        if (res.web_dashboard->client_fd < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                svcSleepThread(1000000);
                continue;
            }
            else {
                break;
            }
        }

        char* template = safe_strdup(html);
        if (!template) return NULL;
        
        char threads_buf[12];
        char hashrate_buf[64];
        char diff_buf[64];
        char shares_buf[64];
        char sensors_buf[48];


        snprintf(hashrate_buf, sizeof(hashrate_buf), "%.2f", mr.total_hashrate);
        snprintf(diff_buf, sizeof(diff_buf), "%d", (int)mr.avg_difficulty);
        snprintf(shares_buf, sizeof(shares_buf), "%d", mr.total_shares);
        snprintf(sensors_buf, sizeof(sensors_buf), "Temperature: %.2f*C Battery charge: %d%%", res.skin_temp_milli_c / 1000.0f, res.charge);
        snprintf(threads_buf, sizeof(threads_buf), "%i", mc.threads);

        replace_placeholder(&template, "@@DEVICE@@", "Nintendo Switch");
        replace_placeholder(&template, "@@HASHRATE@@", hashrate_buf);
        replace_placeholder(&template, "@@DIFF@@", diff_buf);
        replace_placeholder(&template, "@@SHARES@@", shares_buf);
        replace_placeholder(&template, "@@NODE@@", mc.node);
        replace_placeholder(&template, "@@ID@@", mc.rig_id);
        replace_placeholder(&template, "@@VERSION@@", APP_VERSION);
        replace_placeholder(&template, "@@SENSOR@@", sensors_buf);
        replace_placeholder(&template, "@@THREADS@@", threads_buf);

        send(res.web_dashboard->client_fd, template, strlen(template), 0);
        free(template);
        close(res.web_dashboard->client_fd);
        svcSleepThread(1000000);
    }
    return NULL;
}

/**
 * @brief entry point
 * @return exit status
 * @note initializes systems, starts threads, and manages main loop
 */
int main() {
    consoleInit(NULL);

    // set up joycons
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    // prevent sleeping
    appletSetAutoSleepDisabled(true);
    appletSetTvPowerStateMatchingMode(AppletTvPowerStateMatchingMode_Unknown1);

    socketInitializeDefault();

    //for nxlink server
    nxlinkStdio();

    // parse config
    parse_config_file(&mc);
    consoleUpdate(NULL);
    sleep(1);

    // find a node if node not set
    if (mc.node == NULL || mc.port == 0) {
        get_node(&mc.node, &mc.port);
    }

    // toggle CPU boost
    if (mc.cpu_boost) {
        appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
    }

    // init temperature
    Result tcrc = tcInitialize();
    if (R_FAILED(tcrc)) {
        cleanup("failed to initialize tc");
    }

    // init battery management
    Result psmrc = psmInitialize();
    if (R_FAILED(psmrc)) {
        cleanup("failed to initialize psm");
    }

    // allocate thread data
    res.mining_threads = safe_malloc(mc.threads * sizeof(pthread_t));
    res.thread_data = safe_malloc(mc.threads * sizeof(ThreadData));
    srand(time(NULL));
    res.single_miner_id = rand() % 2812; ///< this is to combine multithreaded workloads to appear as one single device in the wallet.

    if (!res.mining_threads || !res.thread_data) {
        cleanup("memory allocation failed");
    }

    //create web dashboard
    if (mc.web_dashboard) {
        res.web_dashboard = malloc(sizeof(WebDashboard));
        if (pthread_create(&res.web_dashboard->wd_thread, NULL, web_dashboard, (void*)res.web_dashboard) != 0) {
            cleanup("failed to start web dashboard thread");
        }
    }
    

    // create mining threads
    for (int i = 0; i < mc.threads; i++) {
        res.thread_data[i].thread_id = i;

        if (pthread_create(&res.mining_threads[i], NULL, do_mining_work, (void*)&res.thread_data[i]) != 0) {
            cleanup("failed to start mining thread");
        }
        sleep(1); // stagger thread creation
    }

    char timebuf[16];
    time_t last_draw = 0;
    while (appletMainLoop()) {

        //handle joycon input
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus) {
            cleanup(NULL);
            break;
        }

        time_t current_time;
        time(&current_time);
        //draw display once every two seconds
        if (difftime(current_time, last_draw) >= 2) {
            get_time_string(timebuf, sizeof(timebuf));

            //charging behavior
            PsmChargerType chargeType;
            psmrc = psmGetChargerType(&chargeType);
            res.chargeType = safe_strdup(get_psm_charger_type(chargeType));

            //charge percentage
            psmrc = psmGetBatteryChargePercentage(&res.charge);
            
            //temp
            tcrc = tcGetSkinTemperatureMilliC(&res.skin_temp_milli_c);

            mr.total_shares = 0;
            mr.total_hashrate = 0;
            mr.total_difficulty = 0;
            mr.good_shares = 0;
            mr.bad_shares = 0;
            mr.blocks = 0;
            for (int i = 0; i < mc.threads; i++) {
                mr.total_hashrate += res.thread_data[i].hashrate;
                mr.total_difficulty += res.thread_data[i].difficulty;
                mr.total_shares += res.thread_data[i].total_shares;
                mr.good_shares += res.thread_data[i].good_shares;
                mr.bad_shares += res.thread_data[i].bad_shares;
                mr.blocks += res.thread_data[i].blocks;
            }
            mr.avg_difficulty = (float)mr.total_difficulty / mc.threads;

            printf(CONSOLE_ESC(2J)); // clear screen

            printf(CONSOLE_ESC(1;1H) "Node: %s:%i", mc.node, mc.port);
            printf(CONSOLE_ESC(2;1H) "Current Time: %s", timebuf);

            //row 3 lb

            if (res.charge > 25) {
                printf(CONSOLE_ESC(4;1H) "Battery Level: %u%% (%s)", res.charge, res.chargeType);
            }
            else {
                printf(CONSOLE_ESC(4;1H) ERROR_RED "Battery Level: %u%% " RESET " (%s)" , res.charge, res.chargeType);
            }

            if (res.skin_temp_milli_c / 1000.0f > 55.0f) {
                printf(CONSOLE_ESC(5;1H) ERROR_RED "Temperature: %.2f C" RESET, res.skin_temp_milli_c / 1000.0f);
            }
            else {
                printf(CONSOLE_ESC(5;1H) "Temperature: %.2f C", res.skin_temp_milli_c / 1000.0f);
            }
            // row 6 lb
            printf(CONSOLE_ESC(7;1H)  "Rig ID: %s", mc.rig_id);
            //row 8 lb
            printf(CONSOLE_ESC(9;1H)  "Hashrate: %.2f kH/s %s", mr.total_hashrate, mc.cpu_boost ? "(CPU Boosted)" : "");
            printf(CONSOLE_ESC(10;1H) "Difficulty: %d", (int)mr.avg_difficulty);
            // row 11 lb
            printf(CONSOLE_ESC(12;1H) "Shares");
            printf(CONSOLE_ESC(13;1H) LIGHT_GREY "|_ " RESET "Last share: %i", res.last_share);
            printf(CONSOLE_ESC(14;1H) LIGHT_GREY "|_ " RESET "Total: %i", mr.total_shares);
            printf(CONSOLE_ESC(15;1H) LIGHT_GREY "|_ " RESET "Accepted: %i", mr.good_shares);
            printf(CONSOLE_ESC(16;1H) LIGHT_GREY "|_ " RESET "Rejected: %i", mr.bad_shares);
            printf(CONSOLE_ESC(17;1H) LIGHT_GREY "|_ " RESET "Accepted %i/%i Rejected (%d%% Accepted)",
                mr.good_shares, mr.bad_shares, (int)((double)mr.good_shares / mr.total_shares * 100));
            printf(CONSOLE_ESC(18;1H) LIGHT_GREY "|_ " RESET "Blocks Found: %i", mr.blocks);
            //row 19 lb
            //thread info - removed alternate formatting for singlethreaded.
            printf(CONSOLE_ESC(20;1H) "Threads (%i)", mc.threads);
            int startLine = 21;
            for (int i = 0; i < mc.threads; i++) {
                if (res.thread_data[i].error) {
                    printf(CONSOLE_ESC_NSTR("%d;1H") "%i" LIGHT_GREY "|_ " ERROR_RED "ERROR: %s", startLine + (i * 4), i, res.thread_data[i].error);
                    printf(CONSOLE_ESC_NSTR("%d;1H") LIGHT_GREY " |_ " ERROR_RED "Difficulty: %i", startLine + (i * 4) + 1, res.thread_data[i].difficulty);
                    printf(CONSOLE_ESC_NSTR("%d;1H") LIGHT_GREY " |_ " ERROR_RED "Accepted %i/%i Rejected", startLine + (i * 4) + 2,
                        res.thread_data[i].good_shares, res.thread_data[i].bad_shares);
                    printf(RESET);
                }
                else {
                    printf(CONSOLE_ESC_NSTR("%d;1H") "%i" LIGHT_GREY "|_ " RESET "Hashrate: %.2f kH/s", startLine + (i * 4), i, res.thread_data[i].hashrate);
                    printf(CONSOLE_ESC_NSTR("%d;1H") LIGHT_GREY " |_ " RESET "Difficulty: %i", startLine + (i * 4) + 1, res.thread_data[i].difficulty);
                    printf(CONSOLE_ESC_NSTR("%d;1H") LIGHT_GREY " |_ " RESET "Accepted %i/%i Rejected", startLine + (i * 4) + 2,
                        res.thread_data[i].good_shares, res.thread_data[i].bad_shares);
                }
            }

            //logo
            printf(DUCO_ORANGE);
            printf(CONSOLE_ESC(1;53H)  "         ########          ");
            printf(CONSOLE_ESC(2;53H)  "      ###############      ");
            printf(CONSOLE_ESC(3;53H)  "    ###################    ");
            printf(CONSOLE_ESC(4;53H)  "   #####         #######   ");
            printf(CONSOLE_ESC(5;53H)  "  #############    ######  ");
            printf(CONSOLE_ESC(6;53H)  " #######       ###   ##### ");
            printf(CONSOLE_ESC(7;53H)  " ############   ##   ##### ");
            printf(CONSOLE_ESC(8;53H)  " ############   ##   ##### ");
            printf(CONSOLE_ESC(9;53H)  " #######       ###   ##### ");
            printf(CONSOLE_ESC(10;53H) "  #############    ######  ");
            printf(CONSOLE_ESC(11;53H) "   #####         #######   ");
            printf(CONSOLE_ESC(12;53H) "    ###################    ");
            printf(CONSOLE_ESC(13;53H) "      ###############      ");
            printf(CONSOLE_ESC(14;53H) "          #######          ");
            printf(RESET);
            printf(CONSOLE_ESC(15;52H) "github.com/tbwcjw/nxducominer");

            //version string
            printf(CONSOLE_ESC(80;67H) DARK_GREY "%s" RESET, APP_VERSION);
            
            //exit helper
            printf(CONSOLE_ESC(80;1H) NOTICE_BLUE "Press [+] to exit..." RESET);


            last_draw = current_time;

            consoleUpdate(NULL);
        }
        svcSleepThread(1000000);
    }
}

