#include "utilities/http.hpp"
#include "utilities/client.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

std::string trim(const std::string& str)
{
    const std::string whitespace = " \t\n\r";
    size_t start = str.find_first_not_of(whitespace);
    size_t end = str.find_last_not_of(whitespace);

    if (start == std::string::npos) // No non-whitespace characters found
        return "";

    return str.substr(start, end - start + 1);
}

namespace HTTP {

// Converts a method string to HttpMethod enum
HttpMethod StringToHttpMethod(const std::string& methodStr) {
    if (methodStr == "GET") return HttpMethod::GET;
    else if (methodStr == "POST") return HttpMethod::POST;
    else if (methodStr == "PUT") return HttpMethod::PUT;
    else if (methodStr == "DELETE") return HttpMethod::DELETE;
    else if (methodStr == "OPTIONS") return HttpMethod::OPTIONS;
    else if (methodStr == "HEAD") return HttpMethod::HEAD;
    else if (methodStr == "PATCH") return HttpMethod::PATCH;
    else return HttpMethod::INVALID;
}

// Converts HttpMethod enum to a method string
std::string HttpMethodToString(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET: return "GET";
        case HttpMethod::POST: return "POST";
        case HttpMethod::PUT: return "PUT";
        case HttpMethod::DELETE: return "DELETE";
        case HttpMethod::OPTIONS: return "OPTIONS";
        case HttpMethod::HEAD: return "HEAD";
        case HttpMethod::PATCH: return "PATCH";
        default: return "INVALID";
    }
}

HTTPREQUEST ParseHttpRequest(const std::string& requestStr)
{
    HTTPREQUEST request;
    std::istringstream requestStream(requestStr);
    std::string requestLine;
    std::getline(requestStream, requestLine);

    // Remove trailing carriage return if present
    if (!requestLine.empty() && requestLine.back() == '\r')
    {
        requestLine.pop_back();
    }

    std::istringstream requestLineStream(requestLine);

    // Read the method, URI, and protocol from the request line
    std::string methodStr;
    requestLineStream >> methodStr >> request.uri >> request.protocol;
    request.method = StringToHttpMethod(methodStr);

    std::string line;
    // Parse headers
    while (std::getline(requestStream, line) && line != "\r")
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty())
            break;

        size_t delimiterPos = line.find(':');
        if (delimiterPos != std::string::npos)
        {
            std::string headerName = trim(line.substr(0, delimiterPos));
            std::string headerValue = trim(line.substr(delimiterPos + 1));
            request.headers[headerName] = headerValue;
        }
    }

    // Read the body
    std::ostringstream bodyStream;
    bodyStream << requestStream.rdbuf();
    request.body = bodyStream.str();

    return request;
}

std::string GenerateResponse(const HTTPREQUEST& request)
{
    switch (request.method)
    {
        case HttpMethod::GET:
            return HandleGetRequest(request);
        case HttpMethod::POST:
            return HandlePostRequest(request);
        case HttpMethod::PUT:
            return HandlePutRequest(request);
        case HttpMethod::DELETE:
            return HandleDeleteRequest(request);
        default:
            return HandleInvalidMethod(request);
    }
}

std::string HandleInvalidMethod(const HTTPREQUEST& request)
{
    return GenerateErrorResponse("405 Method Not Allowed", 405);
}

std::string HandleGetRequest(const HTTPREQUEST& request)
{
    std::string uri = request.uri;
    if (uri == "/")
        uri = "/index.html";
    std::string filePath = "public" + uri;

    // Check if the file exists
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
        return GenerateErrorResponse("404 Not Found", 404);

    // Read file into a string
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string fileContent = ss.str();

    // Create the response
    HTTPRESPONSE httpResponse;
    httpResponse.protocol = request.protocol;
    httpResponse.statusCodeNumber = 200;
    httpResponse.reasonPhrase = statusCode.at(httpResponse.statusCodeNumber);
    httpResponse.contentType = GetMimeType(filePath);
    httpResponse.body = fileContent;

    std::ostringstream responseStream;
    responseStream << httpResponse.protocol << " " << httpResponse.statusCodeNumber << " " << httpResponse.reasonPhrase << "\r\n";
    responseStream << "Content-Type: " << httpResponse.contentType << "\r\n";
    responseStream << "Content-Length: " << httpResponse.body.size() << "\r\n";
    responseStream << "\r\n";
    responseStream << httpResponse.body;

    return responseStream.str();
}

std::string HandlePostRequest(const HTTPREQUEST& request)
{
    // Process the data (for demonstration, we'll just echo the request body)

    HTTPRESPONSE httpResponse;
    httpResponse.protocol = request.protocol;
    httpResponse.statusCodeNumber = 200;
    httpResponse.reasonPhrase = statusCode.at(httpResponse.statusCodeNumber);
    httpResponse.contentType = "text/html";
    httpResponse.body = "<html><body><h1>POST Data Received</h1><pre>" + request.body + "</pre></body></html>";

    std::ostringstream responseStream;
    responseStream << httpResponse.protocol << " " << httpResponse.statusCodeNumber << " " << httpResponse.reasonPhrase << "\r\n";
    responseStream << "Content-Type: " << httpResponse.contentType << "\r\n";
    responseStream << "Content-Length: " << httpResponse.body.size() << "\r\n";
    responseStream << "\r\n";
    responseStream << httpResponse.body;

    return responseStream.str();
}

std::string HandlePutRequest(const HTTPREQUEST& request)
{
    // Process the data

    HTTPRESPONSE httpResponse;
    httpResponse.protocol = request.protocol;
    httpResponse.statusCodeNumber = 200;
    httpResponse.reasonPhrase = statusCode.at(httpResponse.statusCodeNumber);
    httpResponse.contentType = "text/html";
    httpResponse.body = "<html><body><h1>Data Processed Successfully</h1></body></html>";

    std::ostringstream responseStream;
    responseStream << httpResponse.protocol << " " << httpResponse.statusCodeNumber << " " << httpResponse.reasonPhrase << "\r\n";
    responseStream << "Content-Type: " << httpResponse.contentType << "\r\n";
    responseStream << "Content-Length: " << httpResponse.body.size() << "\r\n";
    responseStream << "\r\n";
    responseStream << httpResponse.body;

    return responseStream.str();
}

std::string HandleDeleteRequest(const HTTPREQUEST& request)
{
    // Process the data

    HTTPRESPONSE httpResponse;
    httpResponse.protocol = request.protocol;
    httpResponse.statusCodeNumber = 200;
    httpResponse.reasonPhrase = statusCode.at(httpResponse.statusCodeNumber);
    httpResponse.contentType = "text/html";
    httpResponse.body = "<html><body><h1>Resource Deleted Successfully</h1></body></html>";

    std::ostringstream responseStream;
    responseStream << httpResponse.protocol << " " << httpResponse.statusCodeNumber << " " << httpResponse.reasonPhrase << "\r\n";
    responseStream << "Content-Type: " << httpResponse.contentType << "\r\n";
    responseStream << "Content-Length: " << httpResponse.body.size() << "\r\n";
    responseStream << "\r\n";
    responseStream << httpResponse.body;

    return responseStream.str();
}

std::string GenerateErrorResponse(const std::string& errorMessage, int errorCode)
{
    HTTPRESPONSE httpResponse;
    httpResponse.protocol = "HTTP/1.1";
    httpResponse.statusCodeNumber = errorCode;
    httpResponse.reasonPhrase = statusCode.at(httpResponse.statusCodeNumber);
    httpResponse.contentType = "text/html";
    httpResponse.body = "<html><body><h1>" + errorMessage + "</h1></body></html>";

    std::ostringstream responseStream;
    responseStream << httpResponse.protocol << " " << httpResponse.statusCodeNumber << " " << httpResponse.reasonPhrase << "\r\n";
    responseStream << "Content-Type: " << httpResponse.contentType << "\r\n";
    responseStream << "Content-Length: " << httpResponse.body.size() << "\r\n";
    responseStream << "\r\n";
    responseStream << httpResponse.body;

    return responseStream.str();
}

std::string GetMimeType(const std::string& filename)
{
    std::string fileExtension = filename.substr(filename.find_last_of('.') + 1);
    if (fileExtension == "html")
        return "text/html";
    else if (fileExtension == "css")
        return "text/css";
    else if (fileExtension == "js")
        return "application/javascript";
    else if (fileExtension == "jpg" || fileExtension == "jpeg")
        return "image/jpeg";
    else if (fileExtension == "png")
        return "image/png";
    else if (fileExtension == "gif")
        return "image/gif";
    else
        return "application/octet-stream";
}

std::string GenerateHttpRequestString(const HTTPREQUEST& request)
{
    std::ostringstream requestStream;

    // Start with the request line
    requestStream << HttpMethodToString(request.method) << " " << request.uri << " " << request.protocol << "\r\n";

    // Add headers
    for (const auto& header : request.headers)
    {
        requestStream << header.first << ": " << header.second << "\r\n";
    }

    // End headers section
    requestStream << "\r\n";

    // Add body if present
    requestStream << request.body;

    return requestStream.str();
}

HTTPRESPONSE ParseHttpResponse(const std::string& responseStr)
{
    HTTPRESPONSE response;
    std::istringstream responseStream(responseStr);
    std::string statusLine;
    std::getline(responseStream, statusLine);

    // Remove trailing carriage return if present
    if (!statusLine.empty() && statusLine.back() == '\r')
    {
        statusLine.pop_back();
    }

    // Parse the status line
    std::istringstream statusLineStream(statusLine);
    statusLineStream >> response.protocol >> response.statusCodeNumber;
    std::getline(statusLineStream, response.reasonPhrase);
    response.reasonPhrase = trim(response.reasonPhrase);

    // Parse headers
    std::string line;
    while (std::getline(responseStream, line) && line != "\r")
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (line.empty())
            break;

        size_t delimiterPos = line.find(':');
        if (delimiterPos != std::string::npos)
        {
            std::string headerName = trim(line.substr(0, delimiterPos));
            std::string headerValue = trim(line.substr(delimiterPos + 1));
            if (headerName == "Content-Type")
            {
                response.contentType = headerValue;
            }
            // You can store other headers if needed
        }
    }

    // Read the body
    std::ostringstream bodyStream;
    bodyStream << responseStream.rdbuf();
    response.body = bodyStream.str();

    return response;
}


HTTPRESPONSE SendHttpRequestSSL(const HTTPREQUEST& request, const std::string& certFile = "", const std::string& keyFile = "")
{
    HTTPRESPONSE response;
    SSL_CTX* ctx = nullptr;
    SSL* ssl = nullptr;

    try
    {
        // Initialize OpenSSL
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();

        // Create a new SSL context
        ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx)
        {
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("Unable to create SSL context");
        }

        // Load client certificate and private key if provided
        if (!certFile.empty() && !keyFile.empty())
        {
            if (SSL_CTX_use_certificate_file(ctx, certFile.c_str(), SSL_FILETYPE_PEM) <= 0)
            {
                ERR_print_errors_fp(stderr);
                throw std::runtime_error("Failed to load client certificate");
            }

            if (SSL_CTX_use_PrivateKey_file(ctx, keyFile.c_str(), SSL_FILETYPE_PEM) <= 0)
            {
                ERR_print_errors_fp(stderr);
                throw std::runtime_error("Failed to load client private key");
            }
        }
        // Set default paths for trusted CA certificates
        if (!SSL_CTX_set_default_verify_paths(ctx))
        {
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("Failed to set default verify paths");
        }
        // Set verification mode
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
        // Create a new SSL structure for a connection
        ssl = SSL_new(ctx);
        if (!ssl)
        {
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("Unable to create SSL structure");
        }
        // Use the Client class to create and connect the socket
        Networking::Client client(request.headers.at("Host").c_str(), 443); 
        // Get the underlying socket file descriptor
        SOCKET clientSocket = client.GetConnectionSocket();
        if (INVALIDSOCKET(clientSocket))
        {
            throw std::runtime_error("Invalid client socket");
        }
        // Associate the socket with the SSL structure
        if (SSL_set_fd(ssl, static_cast<int>(clientSocket)) == 0)
        {
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("Failed to set SSL file descriptor");
        }
        // Perform the SSL/TLS handshake
        int ret = SSL_connect(ssl);
        if (ret <= 0)
        {
            int errorCode = SSL_get_error(ssl, ret);
            std::cerr << "SSL_connect failed with error code: " << errorCode << std::endl;
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("SSL handshake failed");
        }
        // Generate the HTTP request string
        std::string requestStr = GenerateHttpRequestString(request);

        // Send the request over the SSL connection
        int bytesSent = SSL_write(ssl, requestStr.c_str(), static_cast<int>(requestStr.size()));
        if (bytesSent <= 0)
        {
            int errorCode = SSL_get_error(ssl, bytesSent);
            std::cerr << "SSL_write failed with error code: " << errorCode << std::endl;
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("Failed to send SSL data");
        }
        // Receive the response over the SSL connection
        const int bufferSize = 4096;
        char buffer[bufferSize];
        int bytesRead=0;
        std::string responseStr;
       
      do
{
    bytesRead = SSL_read(ssl, buffer, bufferSize);
    if (bytesRead > 0)
    {
        responseStr.append(buffer, bytesRead);
       
    }
    else if (bytesRead == 0)
    {
        std::cout << "SSL_read returned 0 (connection closed)." << std::endl;
        // Connection closed
        break;
    }
    else
    {
        int errorCode = SSL_get_error(ssl, bytesRead);
        std::cerr << "SSL_read failed with error code: " << errorCode << std::endl;
        if (errorCode == SSL_ERROR_WANT_READ || errorCode == SSL_ERROR_WANT_WRITE)
        {
            // Retry
            continue;
        }
        else
        {
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("Failed to read SSL data");
        }
    }
} while (bytesRead > bufferSize);

        // Parse the response
        response = ParseHttpResponse(responseStr);

        // Shutdown SSL connection
        SSL_shutdown(ssl);

        // Clean up SSL resources
        SSL_free(ssl);
        SSL_CTX_free(ctx);

        // Disconnect the client
        client.Disconnect();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception in SendHttpRequestSSL: " << e.what() << std::endl;
        response.statusCodeNumber = 0;
        response.reasonPhrase = "Exception";

        // Clean up SSL resources in case of an exception
        if (ssl)
        {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
        if (ctx)
        {
            SSL_CTX_free(ctx);
        }
    }

    // Clean up OpenSSL
    ERR_free_strings();
    EVP_cleanup();

    return response;
}


} // namespace HTTP