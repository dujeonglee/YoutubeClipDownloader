#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RESPONSE_DATA_SIZE (1024*64)
#define MAX_RESPONSE_HEADER_SIZE (1024)
#define MAX_URL_SIZE (1024*10)
#define MAX_FILENAME_SIZE 64

#define YT_CLIP_TYPE_UNKNOWN 0
#define YT_CLIP_TYPE_WEBM 1
#define YT_CLIP_TYPE_MP4 2
#define YT_CLIP_TYPE_3GPP 3
#define YT_CLIP_TYPE_FLV 4

#define MY_DBG			printf("%s: %u\n", __func__, __LINE__)

#define UNUSED(x)            x=x

unsigned int bytes_written;

struct ResponseBuffer
{
    char buffer[MAX_RESPONSE_DATA_SIZE];
    unsigned int total_size;
    char* header;
    char* payload;
};

static size_t write_to_buffer(char *buffer, size_t size, size_t nitems, void *userdata)
{
    if(((struct ResponseBuffer*)userdata)->total_size + size*nitems < MAX_RESPONSE_DATA_SIZE)
    {
        memcpy(((struct ResponseBuffer*)userdata)->buffer+((struct ResponseBuffer*)userdata)->total_size, buffer, size*nitems);
        ((struct ResponseBuffer*)userdata)->total_size += size * nitems;
    }
    return size*nitems;
}

static size_t write_to_file(char *buffer, size_t size, size_t nitems, void *userdata)
{
    bytes_written += size*nitems;
    printf("\rReceived : %u", bytes_written);
    fflush(stdout);
    return fwrite(buffer, size, nitems, (FILE*)userdata);
}


CURL *curl = NULL;
bool my_curl_init()
{
    if(curl)
    {
        return true;
    }
    if(curl_global_init(CURL_GLOBAL_ALL) != 0)
    {
        return false;
    }
    curl = curl_easy_init();
    if(curl == NULL)
    {
        curl_global_cleanup();
        return false;
    }
    return true;
}

void my_curl_cleanup()
{
    if(curl == NULL)
    {
        return;
    }
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    curl = NULL;
}

bool my_curl_request(char* url, struct ResponseBuffer* buff, bool include_hdr)
{
    if(curl == NULL)
    {
        return false;
    }

    memset(buff->buffer, 0x0, MAX_RESPONSE_DATA_SIZE);
    buff->total_size = 0;
    buff->header = NULL;
    buff->payload = NULL;

    if(curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK)
    {
        return false;
    }
    if(curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)buff))
    {
        return false;
    }
    if(curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_buffer))
    {
        return false;
    }
    if(include_hdr)
    {
        if(curl_easy_setopt(curl, CURLOPT_HEADERDATA, buff) != CURLE_OK)
        {
            return false;
        }
        if(curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_to_buffer) != CURLE_OK)
        {
            return false;
        }
    }
    if(curl_easy_perform(curl) != CURLE_OK)
    {
        return false;
    }
    if(include_hdr)
    {
        buff->header = buff->buffer;
        buff->payload = strstr(buff->buffer, "\r\n\r\n");
        memset(buff->payload, 0x0, 4/*\r\n\r\n*/);
        buff->payload += 4/*\r\n\r\n*/;
    }
    else
    {
        buff->header = NULL;
        buff->payload = buff->buffer;
    }
    return true;
}

bool my_curl_request(char* url, char* filename, bool include_hdr)
{
    FILE* file;
    if(curl == NULL)
    {
        return false;
    }

    file = fopen(filename, "wb");
    if(file == NULL)
    {
        return false;
    }
    if(curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK)
    {
        fclose(file);
        return false;
    }
    if(curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)file))
    {
        fclose(file);
        return false;
    }
    if(curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file))
    {
        fclose(file);
        return false;
    }
    if(include_hdr)
    {
        if(curl_easy_setopt(curl, CURLOPT_HEADERDATA, file) != CURLE_OK)
        {
            fclose(file);
            return false;
        }
        if(curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_to_file) != CURLE_OK)
        {
            fclose(file);
            return false;
        }
    }
    bytes_written = 0;
    if(curl_easy_perform(curl) != CURLE_OK)
    {
        fclose(file);
        return false;
    }
    fclose(file);
    return true;
}

bool my_curl_find_value(const char* key, char* in, char* out, unsigned int out_size)
{
    memset(out, 0x0, out_size);
    char* start = strstr(in, key);
    char* end = strchr(start+1, '&');
    if(end == 0)
    {
        end = strchr(start+1, 0);
    }
    if(end == NULL)
    {
        return false;
    }
    if((end - start)+1 > out_size)
    {
        return false;
    }
    strncpy(out, start+strlen(key), (end - start)-strlen(key));
    return true;
}

bool my_curl_url_decoding(char* in, char* out, unsigned int out_size)
{
    int decoded_url_size;
    memset(out, 0x0, out_size);
    if(curl == NULL)
    {
        return false;
    }
    char* curl_decode = curl_easy_unescape(curl, in, strlen(in), &decoded_url_size);
    if((unsigned int)decoded_url_size > out_size)
    {
        curl_free(curl_decode);
        return false;
    }
    strcpy(out, curl_decode);
    curl_free(curl_decode);
    return true;
}

int main(int argc, char* argv[])
{
    if(argc != 4)
    {
        printf("Usage: %s url {webm, mp4, 3gpp, flv} filename\n", argv[0]);
        return -1;
    }
    if(my_curl_init() == false)
    {
        return -1;
    }

    char video_id[16] = {0,};
    char query[64] = {0,};
    struct ResponseBuffer response;
    char stream_map[1024*64] = {0,};
    char decoded_stream_map[1024*64] = {0,};
    char decoded_video[1024*32] = {0,};
    char download_url[1024*32] = {0,};


    sscanf(argv[1], "https://www.youtube.com/watch?v=%s", video_id);
    sprintf(query, "https://www.youtube.com/get_video_info?video_id=%s", video_id);
    if(my_curl_request(query, &response, false) == false)
    {
        my_curl_cleanup();
        return -1;
    }

    if(my_curl_find_value("url_encoded_fmt_stream_map=", response.payload, stream_map, 1024*64) == false)
    {
        my_curl_cleanup();
        return -1;
    }

    if(my_curl_url_decoding(stream_map, decoded_stream_map, 1024*64) == false)
    {
        my_curl_cleanup();
        return -1;
    }

    char* video = strtok(decoded_stream_map, ",");
    while(video != NULL)
    {
        if(my_curl_url_decoding(video, decoded_video, 1024*32) == true)
        {
            if(strstr(decoded_video, "signature=") != NULL && strstr(decoded_video, argv[2]) != NULL)
            {
                break;
            }
        }
        video = strtok(NULL, ",");
    }

    if(video == NULL)
    {
        my_curl_cleanup();
        printf("The requested content's signature is encrypted or the requested format %s is not supported\n", argv[2]);
        return -1;
    }

    char* url_pos = strstr(decoded_video, "url=");
    strcpy(download_url, url_pos+strlen("url="));
    download_url[strlen(download_url)]='&';
    strncpy(download_url+strlen(download_url), decoded_video, url_pos - decoded_video);
    if(download_url[strlen(download_url)-1]=='&')
    {
        download_url[strlen(download_url)-1]=0;
    }
    char* itag_start = strstr(download_url, "itag=");
    char* itag_end = strstr(itag_start+1, "&");
    if(itag_end == NULL)
    {
        memset(itag_start, 0x0, strlen(itag_start));
    }
    else
    {
        memmove(itag_start, itag_end, strlen(itag_end));
    }
    printf("start download %s\n", download_url);
    if(my_curl_request(download_url, argv[3], false) == false)
    {
        printf("Could not download the content\n");
        my_curl_cleanup();
        return -1;
    }
    my_curl_cleanup();
    return 0;
}

