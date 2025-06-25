#pragma once
#ifndef _HTTP_
#define _HTTP_

#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <fstream>
#include <iostream>

std::string trim(const std::string& str);

namespace HTTP {

// Status codes mapping
const std::unordered_map<int, std::string> statusCode = {
    {200, "OK"},
    {201, "Created"},
    {204, "No Content"},
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {500, "Internal Server Error"}
};

// Define the HttpMethod enum class
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE,
    OPTIONS,
    HEAD,
    PATCH,
    INVALID
};

// Function to convert a string to HttpMethod enum
HttpMethod StringToHttpMethod(const std::string& methodStr);

// Function to convert HttpMethod enum to a string
std::string HttpMethodToString(HttpMethod method);

struct HTTPREQUEST
{
    HttpMethod method;
    std::string uri;
    std::string protocol;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct HTTPRESPONSE
{
    std::string protocol;
    int statusCodeNumber;
    std::string reasonPhrase;
    std::string contentType; 
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

// Function declarations
HTTPREQUEST ParseHttpRequest(const std::string& requestStr);
std::string GenerateResponse(const HTTPREQUEST& request);
std::string HandleGetRequest(const HTTPREQUEST& request);
std::string HandlePostRequest(const HTTPREQUEST& request);
std::string HandlePutRequest(const HTTPREQUEST& request);
std::string HandleDeleteRequest(const HTTPREQUEST& request);
std::string HandleInvalidMethod(const HTTPREQUEST& request);
std::string GenerateErrorResponse(const std::string& errorMessage, int errorCode);
std::string GetMimeType(const std::string& filename);

std::string GenerateHttpRequestString(const HTTPREQUEST& request);
HTTPRESPONSE ParseHttpResponse(const std::string& responseStr);
HTTPRESPONSE SendHttpRequestSSL(const HTTPREQUEST& request, const std::string& certFile, const std::string& keyFile);

} // namespace HTTP

#endif