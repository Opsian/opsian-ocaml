#include <stdio.h>
#include "globals.h"
#include "profiler.h"
#include <execinfo.h>
#include <signal.h>
#include "caml/mlvalues.h"
#include <time.h>

extern "C" CAMLprim void start_opsian_native(
    value ocaml_version_str, value ocaml_executable_name_str, value ocaml_argv0_str);

static char* OCAML_EXE_NAME;
static size_t OCAML_EXE_NAME_LEN;

static char* OCAML_ARGV0;
static size_t OCAML_ARGV0_LEN;

static char* OPTIONS;
static ConfigurationOptions* CONFIGURATION;
static Profiler* prof;

void setup_configuration();

void assign_range(char* value, char* next, std::string& to) {
    size_t size = (next == 0) ? strlen(value) : (size_t) (next - value);
    to.assign(value, size);
}

void parseArguments(char *options, ConfigurationOptions &configuration) {
    char* next = options;
    for (char *key = options; next != nullptr; key = next + 1) {
        char *value = strchr(key, '=');
        next = strchr(key, ',');
        if (value == nullptr) {
            logError("WARN: No value for key %s\n", key);
            continue;
        } else {
            value++;
            if (strstr(key, "__logPath") == key) {
                assign_range(value, next, configuration.logFilePath);
            } else if (strstr(key, "__maxFrames") == key) {
                configuration.maxFramesToCapture = atoi(value);
            } else if (strstr(key, "__port") == key) {
                assign_range(value, next, configuration.port);
            } else if (strstr(key, "host") == key) {
                assign_range(value, next, configuration.host);
            } else if (strstr(key, "apiKey") == key) {
                assign_range(value, next, configuration.apiKey);
            } else if (strstr(key, "agentId") == key) {
                assign_range(value, next, configuration.agentId);
            } else if (strstr(key, "debugLogPath") == key) {
                assign_range(value, next, configuration.debugLogPath);
            } else if (strstr(key, "errorLogPath") == key) {
                assign_range(value, next, configuration.errorLogPath);
            } else if (strstr(key, "applicationVersion") == key) {
                assign_range(value, next, configuration.applicationVersion);
            } else if (strstr(key, "customCertificateFile") == key) {
                assign_range(value, next, configuration.customCertificateFile);
            } else if (strstr(key, "onPremHost") == key) {
                char onPremHostValue = *value;
                configuration.onPremHost = (onPremHostValue == 'y' || onPremHostValue == 'Y');
            } else if (strstr(key, "__logCorruption") == key) {
                char logCorruptionValue = *value;
                configuration.logCorruption = (logCorruptionValue == 'y' || logCorruptionValue == 'Y');
            } else {
                logError("WARN: Unknown configuration option: %s\n", key);
            }
        }
    }
}

void substitute_option(
    std::string& value,
    const std::string& search,
    const std::string& replace) {
    size_t pos = 0;
    while ((pos = value.find(search, pos)) != std::string::npos) {
        value.replace(pos, search.length(), replace);
        pos += replace.length();
    }
}

void substitutePath(ConfigurationOptions* configuration, const char* name,
                    const size_t& len, string substitution_variable) {

    string substitution_value(name, len);
    size_t last_slash = substitution_value.find_last_of('/');
    if (last_slash != std::string::npos) {
        substitution_value.erase(0, last_slash + 1);
    }
    substitute_option(substitution_value, ".", "_");

    substitute_option(configuration->logFilePath, substitution_variable, substitution_value);
    substitute_option(configuration->errorLogPath, substitution_variable, substitution_value);
    substitute_option(configuration->debugLogPath, substitution_variable, substitution_value);
    substitute_option(configuration->customCertificateFile, substitution_variable, substitution_value);
    substitute_option(configuration->agentId, substitution_variable, substitution_value);
}

/*
 * If you use £{EXE_NAME} in your options strings then it should sub in the process name.
 * For example "debugLogPath=£{EXE_NAME}-debug.log" with a program whose executable command is
 * "./_build/default/examples/opsian_examples.exe" results in a debug log file of "opsian_examples_exe-debug.log"
 */
void substitute_options(
    ConfigurationOptions* configuration) {

    substitutePath(configuration, OCAML_EXE_NAME, OCAML_EXE_NAME_LEN, "£{EXE_NAME}");
    substitutePath(configuration, OCAML_ARGV0, OCAML_ARGV0_LEN, "£{ARGV_0}");

    char pid_buffer[100];
    const int pid_len = snprintf(pid_buffer, 100, "%d", getpid());
    substitutePath(configuration, pid_buffer, pid_len, "£{PID}");
}

void crashHandler(int sig) {
    const int MAX_BT_SIZE = 20;
    void *array[MAX_BT_SIZE];
    size_t size;

    size = backtrace(array, MAX_BT_SIZE);

    // print out all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}

// ---------------------
// BEGIN Fork Callbacks
// ---------------------

void prepare_fork() {
    network_prepare_fork();
}

void parent_fork() {
    *_DEBUG_LOGGER << "parent_fork " << getpid() << endl;
    network_parent_fork();
}

void child_fork() {
    setup_configuration();
    _DEBUG_LOGGER->on_fork(CONFIGURATION->debugLogPath);
    *_DEBUG_LOGGER << "child_fork " << getpid() << endl;
    network_child_fork();
    prof->on_fork();
}

// ---------------------
// END Fork Callbacks
// ---------------------

char* copy(char* source, const size_t size) {
    char* result = new char[size + 1];
    strncpy(result, source, size + 1);
    return result;
}

CAMLprim void start_opsian_native(
    value ocaml_version_str, value ocaml_executable_name_str, value ocaml_argv0_str) {
//    signal(SIGSEGV, crashHandler);

    const char* ocaml_version = String_val(ocaml_version_str);

    char* ocaml_exe_name = (char*) String_val(ocaml_executable_name_str);
    OCAML_EXE_NAME_LEN = caml_string_length(ocaml_executable_name_str);
    OCAML_EXE_NAME = copy(ocaml_exe_name, OCAML_EXE_NAME_LEN);

    char* ocaml_argv0 = (char*) String_val(ocaml_argv0_str);
    OCAML_ARGV0_LEN = caml_string_length(ocaml_argv0_str);
    OCAML_ARGV0 = copy(ocaml_argv0, OCAML_ARGV0_LEN);

    std::istringstream(AGENT_VERSION_STR) >> AGENT_VERSION;

    OPTIONS = getenv("OPSIAN_OPTS");
    if (OPTIONS == nullptr) {
        logError("Please set the environment variable 'OPSIAN_OPTS' in order to use Opsian\n");
        return;
    }

    CONFIGURATION = new ConfigurationOptions();
    setup_configuration();

    const std::string& errorLogPath = CONFIGURATION->errorLogPath;
    if (!errorLogPath.empty()) {
        // never de-allocated, lives the life of the program
        ERROR_FILE = new ofstream(errorLogPath);
    }

    prof = new Profiler(CONFIGURATION, ocaml_version);
    pthread_atfork(&prepare_fork, &parent_fork, &child_fork);
    prof->start();
}

void setup_configuration() {
    parseArguments(OPTIONS, *CONFIGURATION);
    substitute_options(CONFIGURATION);
}

void bootstrapHandle(int signum, siginfo_t *info, void *context) {
    IMPLICITLY_USE(info)

    prof->handle(signum, context);
}

void sleep_ms(uint64_t durationInMs) {
    // Uses Posix function.
    timespec duration;
    duration.tv_sec = durationInMs / MS_IN_S;
    duration.tv_nsec = (durationInMs % MS_IN_S) * NS_IN_MS;

    int res = nanosleep(&duration, &duration);
    if (res != 0) {
        logError("Error (%d) invoking nanosleep for %lu", res, durationInMs);
    }
}

// LWT PROTOYPE CODE:
extern "C" {
    #include "linkable_profiler.h"
}

#include <algorithm>
#include "deps/libbacktrace/backtrace.h"
#include <random>

struct backtrace_state* lwt_bt_state = NULL;

void lwtHandleBtError(void* data, const char* errorMessage, int errorNumber) {
    printf("lib_bt error: errorNumber=%d, %s\n", errorNumber, errorMessage);
}

struct LwtLocation {
    uint64_t functionId;
    int lineNumber;
    string fileName;
    string functionName;
    bool lwt;
};

struct PromiseSample {
    int64_t start_time_in_ns;
    uint64_t site_id;
};

struct SiteInformation {
    uint64_t site_id;
    uintptr_t lwt_function;
    vector<LwtLocation> locations;
    int32_t sample_count;
    int64_t total_duration_in_ns;
};

const char* PREFIX = "camlLwt_";
const size_t PREFIX_LEN = strlen(PREFIX);

const int NO_FUNCTION = -1;

unordered_set<uint64_t> lwt_pcs {};
unordered_map<uint64_t, LwtLocation> pcs_to_location {};
unordered_map<int, PromiseSample> promise_id_to_sample{};
const uintptr_t NO_LWT_FUNCTION = 0;
uintptr_t last_lwt_function = NO_LWT_FUNCTION;
vector<LwtLocation> current_locations{};

uint64_t next_site_id = 0;
unordered_map<uint64_t, SiteInformation> site_id_to_information {};
unordered_map<uintptr_t, vector<SiteInformation>> lwt_function_to_site_information {};

std::random_device rd;
std::mt19937 mt(rd());
std::uniform_real_distribution<double> distribution(0.0, 1.0);

void remove(string& value, const string& search) {
    size_t pos = value.find(search);
    if (pos != string::npos) {
        value.erase(pos, search.length());
    }
}

void lwtHandleSyminfo (
    void* data,
    uintptr_t pc,
    const char *symname,
    uintptr_t symval,
    uintptr_t symsize) {

    LwtLocation* location = (LwtLocation*)data;

    if (symname == NULL) {
        printf("Missing symbol name for %lu\n", pc);
        location->functionId = pc;
        location->lineNumber = NO_FUNCTION;
        return;
    }

    location->functionId = pc;
    location->lineNumber = 0;
    location->fileName = "";
    // make a copy, ignoring the "caml" prefix
    location->functionName = string(symname, 4, string::npos);
    remove(location->functionName, "Dune__exe__");
    location->lwt = strncmp(symname, PREFIX, PREFIX_LEN) == 0;
}

// Lookup line number and location
int lwtHandlePcInfo (
    void *data,
    uintptr_t pc,
    const char *filename,
    int lineno,
    const char *function) {

    LwtLocation* location = (LwtLocation*)data;
    location->lineNumber = lineno;
    if (filename != NULL) {
        location->fileName = filename;
    }

    return 0;
}

int64_t toNanos(const timespec& timestamp) {
    return (timestamp.tv_sec * NS_IN_S) + timestamp.tv_nsec;
}

void lwt_check_frame(const uint64_t pc) {
    // Skip over lwt frames
    if (lwt_pcs.count(pc) > 0) {
        last_lwt_function = pc;
        return;
    }

    auto it = pcs_to_location.find(pc);
    if (it != pcs_to_location.end()) {
        current_locations.emplace_back(it->second);
        return;
    }

    // Get the Ocaml function name

    LwtLocation location{};
    backtrace_syminfo(lwt_bt_state, pc, lwtHandleSyminfo, lwtHandleBtError, &location);
    if (location.lineNumber != NO_FUNCTION) {
        // Get the Ocaml file name / line number
        backtrace_pcinfo(lwt_bt_state, pc, lwtHandlePcInfo, lwtHandleBtError, &location);

        pcs_to_location.insert({pc, location});

        // It's part of LWT
        if (location.lwt) {
            lwt_pcs.insert(pc);
            last_lwt_function = pc;
        } else {
            current_locations.emplace_back(location);
        }
    }
}

extern "C" CAMLprim value lwt_sample() {
    const double sample = distribution(mt);
    return sample > 0.5 ? Val_true : Val_false;
}

bool matches(vector<LwtLocation>& left, vector<LwtLocation>& right) {
    if (left.size() != right.size()) {
        return false;
    }

    auto right_it = right.begin();
    for (auto left_it = left.begin(); left_it != left.end(); ++left_it) {
        if (left_it->functionId != right_it->functionId || left_it->lineNumber != right_it->lineNumber) {
            return false;
        }

        ++right_it;
    }

    return true;
}

uint64_t get_site() {
    // TODO: improve efficiency of this
    vector<SiteInformation>& site_informations = lwt_function_to_site_information[last_lwt_function];
    for (SiteInformation& site : site_informations) {
        if (matches(current_locations, site.locations)) {
            return site.site_id;
        }
    }

    uint64_t site_id = next_site_id++;

    SiteInformation siteInformation{};
    siteInformation.site_id = site_id;
    siteInformation.lwt_function = last_lwt_function;
    siteInformation.locations = current_locations;
    siteInformation.sample_count = 0;
    siteInformation.total_duration_in_ns = 0;

    site_id_to_information.insert({site_id, siteInformation});
    site_informations.emplace_back(siteInformation);

    return site_id;
}

extern "C" CAMLprim void lwt_on_create(value ocaml_id) {
    if (lwt_bt_state == NULL) {
        lwt_bt_state = backtrace_create_state(NULL, 0, lwtHandleBtError, NULL);
    }

    CallFrame frames[LWT_MAX_FRAMES];
    int count = lwt_handle(frames);
    int promise_id = Int_val(ocaml_id);

    last_lwt_function = NO_LWT_FUNCTION;
    current_locations.clear();
    for (int i = 0; i < count; i++) {
        if (!frames[i].isForeign) {
            lwt_check_frame(frames[i].frame);
        }
    }

    PromiseSample sample {};

    timespec ts {0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    sample.start_time_in_ns = toNanos(ts);
    sample.site_id = get_site();

    promise_id_to_sample.insert({promise_id, sample});
}

void print_location(const char* prefix, LwtLocation& location) {
    printf("%s%s @ %s:%d\n",
           prefix,
           location.functionName.c_str(),
           location.fileName.c_str(),
           location.lineNumber);
}

void print_site(SiteInformation& siteInformation) {
    printf("Site %lu, %d samples, %ld ns total\n",
           siteInformation.site_id,
           siteInformation.sample_count,
           siteInformation.total_duration_in_ns);

    auto sym_it = pcs_to_location.find(siteInformation.lwt_function);
    if (sym_it != pcs_to_location.end()) {
        print_location("Created via: ", sym_it->second);
    }

    for (LwtLocation& location : siteInformation.locations) {
        print_location("", location);
    }
}

bool duration_comparator(SiteInformation& l, SiteInformation& r) {
    return (l.total_duration_in_ns > r.total_duration_in_ns);
}

void print_site_table() {
    printf("\n\n");
    vector<SiteInformation> all_sites{};
    all_sites.reserve(site_id_to_information.size());
    for (auto entry : site_id_to_information) {
        all_sites.emplace_back(entry.second);
    }
    sort(all_sites.begin(), all_sites.end(), duration_comparator);

    for (auto site : all_sites) {
        print_site(site);
        printf("\n");
    }
    printf("\n");
}

extern "C" CAMLprim void lwt_on_resolve(value ocaml_id) {
    timespec ts {0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t end_time_in_ns = toNanos(ts);

    int promise_id = Int_val(ocaml_id);

    auto it = promise_id_to_sample.find(promise_id);
    if (it != promise_id_to_sample.end()) {
        PromiseSample& sample = it->second;
        int64_t duration_in_ns = end_time_in_ns - sample.start_time_in_ns;
        // printf("\n\nPromise (@%lu) took %ldns\n", sample.site_id, duration_in_ns);

        auto site_it = site_id_to_information.find(sample.site_id);
        if (site_it != site_id_to_information.end()) {
            SiteInformation& siteInformation = site_it->second;
            siteInformation.sample_count++;
            siteInformation.total_duration_in_ns += duration_in_ns;

            // Print at end of the run
            /*if ((promise_id % 12) == 0) {
                print_site_table();
            }*/
            print_site_table();
        } else {
            printf("Missing site: %lu\n", sample.site_id);
        }

        promise_id_to_sample.erase(it);
    }
}

extern "C" CAMLprim void lwt_on_cancel(value ocaml_id) {
    // do the same thing for cancel and resolve for now
    lwt_on_resolve(ocaml_id);
}
