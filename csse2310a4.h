/*
 * csse2310a4.h
 */

#ifndef CSSE2310A4_H
#define CSSE2310A4_H

char** split_by_char(char* str, char split, unsigned int maxFields);

typedef struct {
    char* name;
    char* value;
} HttpHeader;

void free_header(HttpHeader* header);
void free_array_of_headers(HttpHeader** headers);

int parse_HTTP_request(void* buffer, int bufferLen, char** method, 
	char** address, HttpHeader*** headers, char** body);

char* construct_HTTP_response(int status, char* statusExplanation, 
	HttpHeader** headers, char* body);

int parse_HTTP_response(void* buffer, int bufferLen, int* status,
	char** statusExplanation, HttpHeader*** headers, char** body);

#endif
