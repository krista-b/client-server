#include <stdlib.h>
#include <stdio.h>
#include <csse2310a4.h>
#include <csse2310a3.h>
#include <tinyexpr.h>
#include <ctype.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <limits.h>

// Max charactres in a line
#define MAX_LINE 1024

// Error exit codes
#define USAGE 1
#define LISTEN 3
#define VALIDATE 4
#define INTEGRATE 5

// Minimum and maximum values
#define MIN_ARGC 2
#define MAX_ARGC 3
#define MIN_PORTNUM 0
#define MAX_PORTNUM 65535
#define MIN_THR 0
#define NUM_FIELDS 5

// Value of contLen when no headers have been read yet
#define NO_BODY -1

// Charcter literals
#define NEWLINE '\n'
#define CARRIAGE '\r'
#define COMMENT '#'

/* Represents the command line arguments passed to the program.  
 */
typedef struct {
    char* portNum;
    int maxThr;
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

/* Prints associated error message based on the provided error code. Exits 
 * program with code. 
 */
void err_exit(int code) {
    switch (code) {
        case USAGE:
            fprintf(stderr, "Usage: intserver portnum [maxthreads]\n");
            break;
        case LISTEN:
            fprintf(stderr, "intserver: unable to open socket for "
                    "listening\n");
            break;
    }
    exit(code);
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

/* Parses the provided command line arguents into an Args structure. Exits 
 * program if any usage errors occur. This include: not enough arguments, 
 * portnum not being an integer, portnum being out of bounds and number of 
 * threads being out of bounds. 
 *
 * Returns the Args structure generated. 
 */
Args parse_args(int argc, char** argv) {
    if (argc < MIN_ARGC || argc > MAX_ARGC) {
        err_exit(USAGE);
    }
    Args args;
    for (int i = 1; i < argc; i++) {
        for (int j = 0; j < strlen(argv[i]); j++) {
            if (!isdigit(argv[i][j])) {
                err_exit(USAGE);
            }
        }
    }
    args.portNum = argv[1];
    if (atoi(args.portNum) > MAX_PORTNUM || atoi(args.portNum) < MIN_PORTNUM) {
        err_exit(USAGE);
    }
    if (argc == 3) {
        args.maxThr = atoi(argv[2]);
        if (args.maxThr < MIN_THR) {
            err_exit(USAGE);
        }
    } else {
        args.maxThr = INT_MAX;
    }
    return args;
}

/* Checks if the provided expression (func) is a valid expression of x. 
 * Uses provided library: tinyexpr.h to determine this. 
 *
 * Returns false if the expression cannot be evaluated, true otherwise. 
 */
bool valid_func(char* func) {
    double x; 
    te_variable vars[] = {{"x", &x}};
    int errPos;
    te_expr* expr = te_compile(func, vars, 1, &errPos);
    if (expr) {
        te_free(expr);
    } else {
        return false;
    }
    return true;
}

/* Extracts the expression from the provided address and checks if it is a 
 * valid expression. 
 *
 * Returns false if it is not a valid expresion, true otherwise. 
 */
int check_func(char* address) {
    char func[MAX_LINE];
    sscanf(address, "/validate/%s", func);
    if (!valid_func(func)) {
        return false;
    }
    return true;
}

/* Reads from the provided file (f) line by line looking for a complete HTTP 
 * request from the client. Will stop reading when a complete request is read 
 * and stores the entire message in a string. 
 *
 * Returns NULL if a badly formed response is read, returns the string 
 * containing the complete HTTP response read from the file otherwise. 
 */
char* read_request(FILE* f) {
    int len = 0;
    char temp[MAX_LINE];
    char* buffer = NULL;
    int lineNum = 0;
    int contLen = NO_BODY;
    char header[MAX_LINE];
    bool body = false;
    int broken = 0;

    while (fgets(temp, sizeof(temp), f) && !feof(f)) {
        lineNum++;
        buffer = realloc(buffer, sizeof(char) * (len + strlen(temp)));
        strcat(buffer, temp);
        len += strlen(temp);
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
                    broken = 1;
                    break;
                }
            } else {
            }
        }
    }

    if (broken) {
        return NULL;
    }
    return buffer;
}

/* Reads the provided method and address and gets if they are valid. This
 * includes: method being "GET" and address being of the form "/validate/..."
 * or "/integrate/...". 
 *
 * Returns 0 if either the method or address is not valid, VALIDATE if the
 * addressis of the form "validate/.." and INTEGRATE if the adress is of the
 * form "integrate/..."
 */
int check_type(int numRead, char* method, char* address) {
    if (numRead <= 0) {
        return 0;
    }
    if (strcmp(method, "GET")) {
        return 0;
    }
    char ignore[MAX_LINE];
    if (sscanf(address, "/validate/%s", ignore)) {
        return VALIDATE;
    } else if (sscanf(address, "/integrate/%s", ignore)) {
        return INTEGRATE;
    } 
    return 0;
}

/* Checks each of the fields provided or any syntax errors. This includes: not
 * enough arguments, empty arguments, lower or upper bounds not being in
 * floating point format and segments or threads not being an integer. 
 *
 * Returns false if any syntax errors are found, true otherwise. 
 */
bool check_syntax(char** fields, int num) {
    if (num != NUM_FIELDS) {
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

/* Checks the validity of each field within the provided Fields structure. 
 * This includes: no spaces in the function, upper bound is greater than lower
 * bound, segments and threads are greater than zero, segments is a integer
 * multiple of threads and function is a valid expression of x. The appropriate
 * error message is printed if a validation error occurs. 
 *
 * Returns false if any validation errors occur, true otherwise. 
 * */
bool check_validity(Fields fields, int num) {
    for (int i = 0; i < strlen(fields.func); i++) {
        if (isspace(fields.func[i])) {
            return false;
        }
    }
    if (fields.up <= fields.low) {
        return false;
    }
    if (fields.seg <= 0) {
        return false;
    }
    if (fields.thr <= 0) {
        return false;
    }
    if (fields.seg % fields.thr) {
        return false;
    }
    // CHECK FUNC 
    if (!valid_func(fields.func)) {
        return false;
    }
    return true;
}

/* Extracts the expression from the provided address, splits it by '/' and 
 * parses it into a Fields structure. 
 *
 * Returns the fields structure generated. 
 */
Fields get_fields(char* address) {
    char fields[MAX_LINE];
    sscanf(address, "/integrate/%s", fields);
    printf("%s\n", fields);

    char** processed = split_by_char(fields, '/', 0);

    return parse_fields(processed);
}

/* Extracts the expression from the provided address, splits it by '/' and 
 * checks the parts for any syntax errors. Then parses it into a Fields
 * structure and checks that for any validity errors. 
 *
 * Returns false if any syntax or validity errors occur, true otherwise. 
 */
bool check_integrate(char* address) {
    char fields[MAX_LINE];
    sscanf(address, "/integrate/%s", fields);
    char** processed = split_by_char(fields, '/', 0);
    int j = 0;
    while (processed[j]) {
        j++;
    }
    if (!check_syntax(processed, j)) {
        return false;
    }
    Fields f = parse_fields(processed);
    if (!check_validity(f, j)) {
        return false;
    }
    return true;
}

/* Creates a duplicate file descriptor from the provided fd and opens a 
 * reading and writng end to communicate with the client. Reads a request from
 * the client and responds appropriately based on the request contents. This 
 * loops until client is dead. 
 */
void* client_thread(void* arg) {
    int fd = *(int*)arg;
    free(arg);
    int fd2 = dup(fd);
    FILE* to = fdopen(fd, "w");
    FILE* from = fdopen(fd2, "r");

    while (from) {
        int stat = 0;
        char* expl = NULL;
        HttpHeader** reqHeaders = NULL;
        char* reqBody = NULL;
        char* request = malloc(sizeof(char));
        request = read_request(from);
        if (request == NULL) {
            close(fd);
            close(fd2);
            break;
        }
        fflush(from);
        char* method = NULL;
        char* address = NULL;
        HttpHeader** headers = NULL;
        char* body = NULL;
        int numRead = parse_HTTP_request(request, strlen(request), 
                &method, &address, &reqHeaders, &reqBody);
        int type = check_type(numRead, method, address);
        if (type == VALIDATE) {
            if (check_func(address)) {
                stat = 200;
                expl = "OK";
            } 
        } else if (type == INTEGRATE) {
            if (check_integrate(address)) {
                stat = 200;
                expl = "OK";
            } 
        } 
        if (stat == 0) {
            stat = 400;
            expl = "Bad Request";
        }
        char* response = malloc(sizeof(char));
        response = construct_HTTP_response(stat, expl, headers, body);
        fputs(response, to);
        fflush(to);
    }
    close(fd);
    return NULL;
}

int main(int argc, char** argv) {
    Args args;
    args = parse_args(argc, argv);

    struct addrinfo* ai = 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;          
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;  
    int err;
    if ((err = getaddrinfo(NULL, args.portNum, &hints, &ai))) {
        freeaddrinfo(ai);
        err_exit(LISTEN);
    }

    int serv = socket(AF_INET, SOCK_STREAM, 0);
    int v = 1;
    setsockopt(serv, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));

    if (bind(serv, (struct sockaddr*)ai->ai_addr, sizeof(struct sockaddr))) {
        err_exit(LISTEN);
    }

    struct sockaddr_in ad;
    memset(&ad, 0, sizeof(struct sockaddr_in));
    socklen_t len = sizeof(struct sockaddr_in);
    if (getsockname(serv, (struct sockaddr*)&ad, &len)) {
        err_exit(LISTEN);
    }
    fprintf(stderr, "%d\n", ntohs(ad.sin_port));
    fflush(stderr);


    if (listen(serv, INT_MAX)) {  
        err_exit(LISTEN);
    }

    int connFd;
    while (connFd = accept(serv, 0, 0), connFd >= 0) {
	int* fd = malloc(sizeof(int));
	*fd = connFd;
	pthread_t threadId;
	pthread_create(&threadId, NULL, client_thread, fd);
	pthread_detach(threadId);
    }
    
    return 0;
}

