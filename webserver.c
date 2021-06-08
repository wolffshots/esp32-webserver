/*
 * MIT License
 *
 * Copyright (c) 2021 wolffshots
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file component-template.c
 * @brief implementations for component component-template
 */

// rest of the includes
#include "webserver.h"
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_log.h"

#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"

// variables
static const char *TAG = CONFIG_WEBSERVER_LOG_TAG;

/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)

/* Max size of an individual file. Make sure this
 * value is same as that set in upload_script.html */
#define MAX_FILE_SIZE (200 * 1024) // 200 KB
#define MAX_FILE_SIZE_STR "200KB"

/* Scratch buffer size */
#define SCRATCH_BUFSIZE 8192

struct file_server_data
{
    /* Base path of file storage */
    char base_path[ESP_VFS_PATH_MAX + 1];

    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SCRATCH_BUFSIZE];
};

// function definitions
/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
static const char *get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest)
    {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash)
    {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize)
    {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}
/* Handler to redirect incoming GET request for /index.html to /
 * This can be overridden by uploading file with same name */
static esp_err_t index_html_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "307 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0); // Response body can be empty
    return ESP_OK;
}

/* Handler to respond with an icon file embedded in flash.
 * Browsers expect to GET website icon at URI /favicon.ico.
 * This can be overridden by uploading file with same name */
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    extern const unsigned char favicon_ico_start[] asm("_binary_favicon_ico_start");
    extern const unsigned char favicon_ico_end[] asm("_binary_favicon_ico_end");
    const size_t favicon_ico_size = (favicon_ico_end - favicon_ico_start);
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_size);
    return ESP_OK;
}

/* font handler */
static esp_err_t font_get_handler(httpd_req_t *req)
{
    extern const unsigned char font_ico_start[] asm("_binary_Ubuntu_woff2_start");
    extern const unsigned char font_ico_end[] asm("_binary_Ubuntu_woff2_end");
    const size_t font_ico_size = (font_ico_end - font_ico_start);
    httpd_resp_set_type(req, "font/woff2");
    httpd_resp_send(req, (const char *)font_ico_start, font_ico_size);
    return ESP_OK;
}

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".pdf"))
    {
        return httpd_resp_set_type(req, "application/pdf");
    }
    else if (IS_FILE_EXT(filename, ".html"))
    {
        return httpd_resp_set_type(req, "text/html");
    }
    else if (IS_FILE_EXT(filename, ".jpeg"))
    {
        return httpd_resp_set_type(req, "image/jpeg");
    }
    else if (IS_FILE_EXT(filename, ".ico"))
    {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}

off_t ws_get_file_size(const char *filename)
{
    struct stat st;
    if (stat(filename, &st) == 0)
    {
        ESP_LOGI(TAG, "file size: %ld", st.st_size);
        return st.st_size;
    }
    ESP_LOGI(TAG, "Cannot determine size of %s\n",
             filename);
    return -1;
}

static esp_err_t http_resp_index_html(httpd_req_t *req, const char *dirpath)
{
    extern const unsigned char index_start[] asm("_binary_index_html_start");
    extern const unsigned char index_end[] asm("_binary_index_html_end");
    const size_t index_size = (index_end - index_start);
    httpd_resp_send_chunk(req, (const char *)index_start, index_size);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}
static esp_err_t http_resp_robots_txt(httpd_req_t *req, const char *dirpath)
{
    extern const unsigned char robots_start[] asm("_binary_robots_txt_start");
    extern const unsigned char robots_end[] asm("_binary_robots_txt_end");
    const size_t robots_size = (robots_end - robots_start);
    httpd_resp_send_chunk(req, (const char *)robots_start, robots_size);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}
static esp_err_t http_resp_style_css(httpd_req_t *req, const char *dirpath)
{
    extern const unsigned char style_start[] asm("_binary_style_css_start");
    extern const unsigned char style_end[] asm("_binary_style_css_end");
    const size_t style_size = (style_end - style_start);
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send_chunk(req, (const char *)style_start, style_size);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t update_get_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "update received %s", req->uri);
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    char line[20];
    sprintf(line, "%0.3f %0.1f %0.1f %0.1f", temp, goal, under, over);
    httpd_resp_sendstr(req, line);
    return ESP_OK;
}

/* Handler to download a file kept on the server */
static esp_err_t download_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "download for %s", req->uri);
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri, sizeof(filepath));
    if (!filename)
    {
        ESP_LOGE(TAG, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* If name has trailing '/', respond with index.html */
    if (filename[strlen(filename) - 1] == '/')
    {
        return http_resp_index_html(req, filepath);
    }
    // handle style route
    if (strcmp(filename, "/style.css") == 0)
    {
        return http_resp_style_css(req, filepath);
    }
    // handle robots
    if (strcmp(filename, "/robots.txt") == 0)
    {
        return http_resp_robots_txt(req, filepath);
    }
    // handle specifics and 404
    if (stat(filepath, &file_stat) == -1)
    {
        /* If file not present on SPIFFS check if URI
         * corresponds to one of the hardcoded paths */
        if (strcmp(filename, "/index.html") == 0)
        {
            return index_html_get_handler(req);
        }
        else if (strcmp(filename, "/favicon.ico") == 0)
        {
            return favicon_get_handler(req);
        }
        else if (strcmp(filename, "/Ubuntu.woff2") == 0)
        {
            return font_get_handler(req);
        }
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    fd = fopen(filepath, "r");
    if (!fd)
    {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    do
    {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0)
        {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
            {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGI(TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "post received at %s with size %d", req->uri, req->content_len);
    char content[100];
    size_t recv_size = MIN(req->content_len, sizeof(content));
    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0)
    { /* 0 return value indicates connection closed */
        /* Check if timeout occurred */
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            /* In case of timeout one can choose to retry calling
             * httpd_req_recv(), but to keep it simple, here we
             * respond with an HTTP 408 (Request Timeout) error */
            httpd_resp_send_408(req);
        }
        /* In case of error, returning ESP_FAIL will
         * ensure that the underlying socket is closed */
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "post received with content: %s", content);

    if (strcmp(req->uri, "/api/set_temp") == 0)
    {
        goal = atof(content);
        ESP_LOGI(TAG, "new goal as float: %0.2f", atof(content));
        update_display();
    }
    else if (strcmp(req->uri, "/api/set_upper_margin") == 0)
    {
        over = atof(content);
        ESP_LOGI(TAG, "new over as float: %0.2f", atof(content));
        update_display();
    }
    else if (strcmp(req->uri, "/api/set_lower_margin") == 0)
    {
        under = atof(content);
        ESP_LOGI(TAG, "new under as float: %0.2f", atof(content));
        update_display();
    }
    else
    {
        ESP_LOGI(TAG, "post call to %s not handled by implemented checks, respond unsupported", req->uri);
        httpd_resp_set_status(req, "501 Not Implemented");
        httpd_resp_sendstr(req, "couldn't match that req to a server function");
    }

    /* Send a simple response */
    // const char resp[] = "URI POST Response";
    // httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    /* Redirect onto root to see the updated index */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "post processed successfully");
    return ESP_OK;
}
/**
 * function to start the file server
 * @param base_path the local base path to serve files from (eg. "/spiffs")
 * @return error value
 */
esp_err_t start_file_server(const char *base_path)
{
    static struct file_server_data *server_data = NULL;

    /* Validate file storage base path */
    if (!base_path || strcmp(base_path, "/spiffs") != 0)
    {
        ESP_LOGE(TAG, "File server presently supports only '/spiffs' as base path");
        return ESP_ERR_INVALID_ARG;
    }

    if (server_data)
    {
        ESP_LOGE(TAG, "File server already started");
        return ESP_ERR_INVALID_STATE;
    }

    /* Allocate memory for server data */
    server_data = calloc(1, sizeof(struct file_server_data));
    if (!server_data)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for server data");
        return ESP_ERR_NO_MEM;
    }
    strlcpy(server_data->base_path, base_path,
            sizeof(server_data->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    /* Use the URI wildcard matching function in order to
     * allow the same handler to respond to multiple different
     * target URIs which match the wildcard scheme */
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server");
    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start file server!");
        return ESP_FAIL;
    }

    /* URI handler for getting uploaded files */
    httpd_uri_t update = {
        .uri = "/update",
        .method = HTTP_GET,
        .handler = update_get_handler,
        .user_ctx = server_data // Pass server data as context
    };
    httpd_register_uri_handler(server, &update);

    /* URI handler for getting uploaded files */
    httpd_uri_t file_download = {
        .uri = "/*", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = download_get_handler,
        .user_ctx = server_data // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_download);

    /* URI handler for posts to api */
    httpd_uri_t api_post = {
        .uri = "/api/*", // Match all URIs of type /api/path
        .method = HTTP_POST,
        .handler = api_post_handler,
        .user_ctx = server_data // Pass server data as context
    };
    httpd_register_uri_handler(server, &api_post);

    /* URI handler for deleting files from server */
    // httpd_uri_t file_delete = {
    //     .uri       = "/delete/*",   // Match all URIs of type /delete/path/to/file
    //     .method    = HTTP_POST,
    //     .handler   = delete_post_handler,
    //     .user_ctx  = server_data    // Pass server data as context
    // };
    // httpd_register_uri_handler(server, &file_delete);

    return ESP_OK;
}