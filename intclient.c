#include <stdio.h>
#include <stdlib.h>
#include <tinyexpr.h>
#include <csse2310a4.h>
#include <csse2310a3.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <limits.h>

// Maximum characters in a line 
#define MAX_LINE 1024

// Error exit codes
#define USAGE 1
#define CONNECT 2
#define COMMS 3
#define READ 4

// String Characters
#define COMMENT '#'
#define NEWLINE '\n'
#define CARRIAGE '\r'

// Print mode
#define VERBOSE_MODE 1
#define NORMAL_MODE 0

// Minimum values 
#define MIN_FIELDS 5

// Value of contLen when no headers have been read yet
#define NO_BODY -1

/* Represents the command line arguments passed to the program.
 */
typedef struct {
    int verbose;
    const char* portNum;
    char* jobFile;
} Args;

/* Represents the fields included in a job file line. 
 */
typedef struct {
    char* func;
    double low;
    double up;
    int seg;
    int thr;
} Fields;

/* Reads the given processed job file line and parses each field into a 
 * Fields structure. 
 *
 * Returns the Fields structure generated. 
 */
Fields parse_fields(char** processed) {
    Fields fields;
    
    fields.func = processed[0];
    sscanf(processed[1], "%lf", &fields.low);
    sscanf(processed[2], "%lf", &fields.up);
    sscanf(processed[3], "%d", &fields.seg);
    sscanf(processed[4], "%d", &fields.thr);
    return fields;
}

/* Attempts to open the given file for reading if it is not standard in. If 
 * unsuccessful, prints the appropriate error message and exits the program. 
 */
void check_file(char* file) {
    if (strcmp(file, "stdin")) {
        FILE* fd = fopen(file, "r");
        if (!fd) {
            fprintf(stderr, "intclient: unable to open \"%s\" for reading\n", 
                    file);
            exit(READ);
        }
    }
}

/* Checks if the line begins with a # and hence is a comment line. 
 *
 * Returns true if it is a comment and false otherwise. 
 */
bool is_comment(char* line) {
    return line[0] == COMMENT;
}

/* Checks if there are no content in a line and hence is an empty line. 
 *
 * Returns false if there is a character in the line and true otherwise. 
 */
bool is_empty(char* line) {
    for (int i = 0; i < strlen(line); i++) {
        if (!isspace(line[i])) {
            return false;
        }
    }
    return true;
}

/* Parses the provided command line arguments into an Args structure depending
 * on what is present. 
 *
 * Returns the Args structure generated. 
 */
Args parse_args(int argc, char** argv) {
    Args args;
    if (argv[1][0] == '-') {
        if (argv[1][1] == 'v') {
            args.verbose = VERBOSE_MODE;
        }
        args.portNum = argv[2];
    } else {
        args.verbose = NORMAL_MODE;
        args.portNum = argv[1];
    }
    if (args.verbose == VERBOSE_MODE && argc == 4) {
        args.jobFile = argv[3];
    } else if (args.verbose == NORMAL_MODE && argc == 3) {
        args.jobFile = argv[2];
    } else {
        args.jobFile = "stdin";
    }
    return args;
}

/* Checks each of the fields provided or any syntax errors. This includes: not
 * enough arguments, empty arguments, lower or upper bounds not being in
 * floating point format and segments or threads not being an integer. 
 *
 * Returns false if any syntax errors are found, true otherwise. 
 */
bool check_syntax(char** fields, int num) {
    if (num < MIN_FIELDS) {
        return false;
    }
    for (int i = 0; i < num; i++) {
        if (is_empty(fields[i])) {
            return false;
        }
    }
    for (int i = 1; i <= num - 3; i++) {
        double d;
        int n;
        if (!sscanf(fields[i], "%lf%n", &d, &n)) {
            return false;
        }
        if (n != strlen(fields[i])) {

            return false;
        }
        if (d > INT_MAX) {

            return false;
        }
    }
    for (int i = 3; i <= num - 1; i++) {
        int d;
        int n;
        if (!sscanf(fields[i], "%d%n", &d, &n)) {
            return false;
        }
        if (n != strlen(fields[i])) {
            return false;
        }

        char str[MAX_LINE];
        sprintf(str, "%d", d);
        if (strcmp(fields[i], str)) {
            return false;
        }
    }
    return true;
}

/* Allocates memory and builds a null terminated string containing a compete
 * HTTP 1.1 request based on the provided method, address, headers and body.
 *
 * Returns the HTTP request generated. 
 */
char* construct_http_request(char* method, char* address, 
        HttpHeader** headers, char* body) {
    char* request = malloc(sizeof(char) * (strlen("  HTTP/1.1\r\n\r\n") 
            + strlen(method) + strlen(address) + 1));

    if (headers == NULL) {
        sprintf(request, "%s %s HTTP/1.1\r\n\r\n", method, address);
    }
    return request;
}

/* Builds the components of the validation request including the GET method 
 * and validation address containing the provided function (func). Passes this
 * string to construct_http_request to build the HTTP request. 
 *
 * Returns the null terminated string generated by constructing the HTTP 
 * request based on the components passed to it. 
 */
char* make_validation_request(char* func) {
    char* method = malloc(sizeof(char) * (strlen("GET") + 1));
    char* address = malloc(sizeof(char) * (strlen("/validate/") 
            + strlen(func) + 2));
    HttpHeader** headers = NULL;
    char* body = NULL;

    sprintf(method, "GET");
    sprintf(address, "/validate/%s", func);

    return construct_http_request(method, address, headers, body);
}

/* Reads from the provided file (f) line by line looking for a compete HTTP 
 * response from the server. Will stop reading when a complete response is 
 * read and stores the entire message in a string. 
 *
 * Returns the string containing the complete HTTP request read from the file. 
 */
char* read_response(FILE* f) {
    int len = 0;
    char temp[MAX_LINE];
    char* buffer = NULL;
    int lineNum = 0;
    int contLen = NO_BODY;
    char header[MAX_LINE];
    bool body = false;

    while (fgets(temp, sizeof(temp), f)) {
        lineNum++;
        buffer = realloc(buffer, sizeof(char) * (len + strlen(temp)));
        strcat(buffer, temp);
        len += (strlen(temp));
        if (lineNum == 2) {
            if (temp[0] == NEWLINE) {
                break;
            } else {
                sscanf(temp, "%s %d", header, &contLen);
            }
        }
        if (lineNum >= 3) {
            if (temp[0] == NEWLINE || temp[0] == CARRIAGE) {
                if (body) {
                    break;
                }
                if (contLen == 0) {
                    break;
                } else if (contLen > 0) {
                    body = true;
                } else if (contLen == NO_BODY) {
                    fprintf(stderr, "intclient: communications error\n");
                    exit(COMMS);
                }
            } else {
                // other headers
            }
        }
    }
    return buffer;
}

/* Creates a duplicated file descriptior from the provided fd and opens a 
 * reading and writing end to communicate with the server. Sends the server 
 * a validation request and waits for a response. The response is parsed into 
 * HTTP response fields and the status return value is checked. 
 *
 * Returns true if the status is 200, false if the status is 400 and prints an
 * error and exits if any errors occur (repsonse couldn't be parsed or status
 * is something unknown) 
 */
bool check_func(char* func, int fd) {
    int fd2 = dup(fd);
    FILE* to = fdopen(fd, "w");
    FILE* from = fdopen(fd2, "r");
    fputs(make_validation_request(func), to);
    fflush(to);
    
    char* buffer = read_response(from);

    int stat = 0;
    char* expl = NULL;
    HttpHeader** headers = NULL;
    char* body = NULL;

    int numRead = parse_HTTP_response(buffer, strlen(buffer), &stat, 
            &expl, &headers, &body);
    if (numRead) { 
        if (stat == 400) {
            return false;
        } else if (stat == 200) {
            return true;
        } else {
            fprintf(stderr, "intclient: communications error\n");
            exit(COMMS);
        }
    } else {
        fprintf(stderr, "intclient: communications error\n");
        exit(COMMS);
    }

    free(buffer);
    fclose(from);
    return true;
}

/* Checks the validity of each field within the provided Fields structure. 
 * This includes: no spaces in the function, upper bound is greater than lower
 * bound, segments and threads are greater than zero, segments is a integer
 * multiple of threads and function is a valid expression of x. The appropriate
 * error message is printed if a validation error occurs. 
 *
 * Returns false if any validation errors occur, true otherwise. 
 * */
bool check_validity(Fields fields, int num, int lineNum, int fd) {
    for (int i = 0; i < strlen(fields.func); i++) {
        if (isspace(fields.func[i])) {
            fprintf(stderr, "intclient: spaces not permitted in expression " 
                    "(line %d)\n", lineNum);
            return false;
        }
    }
    if (fields.up <= fields.low) {
        fprintf(stderr, "intclient: upper bound must be greater than lower " 
                "bound (line %d)\n", lineNum);
        return false;
    }
    if (fields.seg <= 0) {
        fprintf(stderr, "intclient: segments must be a positive integer " 
                "(line %d)\n", lineNum);
        return false;
    }
    if (fields.thr <= 0) {
        fprintf(stderr, "intclient: threads must be a positive integer " 
                "(line %d)\n", lineNum);
        return false;
    }
    if (fields.seg % fields.thr) {
        fprintf(stderr, "intclient: segments must be an integer multiple of "
                "threads (line %d)\n", lineNum);
        return false;
    }
    // CHECK FUNC 
    if (!check_func(fields.func, fd)) {
        fprintf(stderr, "intclient: bad expression \"%s\" (line %d)\n", 
                fields.func, lineNum);
        return false;
    }
    
    return true;
}

/* Reads from the file at the provided jobFile path line by line, parses the 
 * non-empty line into comma-separated fields and checks the syntax and 
 * validity of line. This is looped until EOF is reached. 
 */
void read_file(char* jobFile, int fd) {
    char line[MAX_LINE];
    int lineNum = 0;
    FILE* file;

    if (!strcmp(jobFile, "stdin")) {
        file = stdin;
    } else {
        file = fopen(jobFile, "r");
    }

    while (fgets(line, sizeof(line), file)) {
        lineNum++;
        if (is_comment(line) || is_empty(line)) {
            continue;
        }
        char** p = split_by_char(line, NEWLINE, 0);
        char** processed = split_by_char(p[0], ',', 0);
        
        int j = 0;
        while (processed[j]) {
            j++;
        }
        
        if (!check_syntax(processed, j)) {
            fprintf(stderr, "intclient: syntax error on line %d\n", lineNum);
            continue;
        }
        Fields fields = parse_fields(processed);
        if (!check_validity(fields, j, lineNum, fd)) {
            continue;
        }
    }
}

/* Checks the provided args structure contains a portNum field. 
 *
 * Returns false if portNum is NULL, true otherwise. 
 */
bool check_args(Args args) {
    if (args.portNum == NULL) {
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "Usage: intclient [-v] portnum [jobfile]\n");
        return USAGE;
    }
    Args args;
    args = parse_args(argc, argv);
    // other usage errors
    if (!check_args(args)) {
        fprintf(stderr, "Usage: intclient [-v] portnum [jobfile]\n");
        return USAGE;
    }

    check_file(args.jobFile);

    //connect to port
    struct addrinfo* ai = 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;   
    hints.ai_socktype = SOCK_STREAM;
    int err;
    if ((err = getaddrinfo("localhost", args.portNum, &hints, &ai))) {
        freeaddrinfo(ai);
        fprintf(stderr, "intclient: unable to connect to port %s\n", 
                args.portNum);
        return CONNECT;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(fd, (struct sockaddr*)ai->ai_addr, sizeof(struct sockaddr))) {
        fprintf(stderr, "intclient: unable to connect to port %s\n", 
                args.portNum);
        return CONNECT;
    }
    
    read_file(args.jobFile, fd);

    return 0;
}
