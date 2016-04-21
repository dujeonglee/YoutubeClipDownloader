#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RESPONSE_BUFFER_SIZE (1024*64)
#define MAX_TITLE_LENGTH 64
#define MAX_VIDEO_ITEMS 32

#define MY_DBG			printf("%s: %u\n", __func__, __LINE__)

struct ResponseBuffer
{
    char buffer[MAX_RESPONSE_BUFFER_SIZE];
    unsigned int payload_size;
};

static size_t write_payload(char *buffer, size_t size, size_t nitems, void *userdata)
{
    ResponseBuffer* const r_buff = (ResponseBuffer*)userdata;
	printf("Write start from %u\n", r_buff->payload_size);
	if(r_buff->payload_size + size*nitems < MAX_RESPONSE_BUFFER_SIZE)
	{
		memcpy(r_buff->buffer+r_buff->payload_size, buffer, size*nitems);
		r_buff->payload_size += size * nitems;
	}
    return size*nitems;
}

static size_t fwrite_payload(char *buffer, size_t size, size_t nitems, void *userdata)
{
    fwrite(buffer, size, nitems, (FILE*)userdata);
    return size*nitems;
}

unsigned int get_urls(char* clip_url, char* title, unsigned int title_size, char* video_request_urls[], unsigned int size)
{
    CURL *curl;
    CURLcode res;
    unsigned int video_items = 0;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if(curl) {
	    ResponseBuffer* response_buffer;
        char clip_id[32] = {0};
        char video_info_query[512] = {0};
        char* curl_title = 0;
        char* curl_stream_map = 0;
        char* curl_video_item_list[MAX_VIDEO_ITEMS] = {0};

		response_buffer = new ResponseBuffer();
        {// 0. Request video information
            sscanf(clip_url, "https://www.youtube.com/watch?v=%s", clip_id);
            sprintf(video_info_query, "https://www.youtube.com/get_video_info?video_id=%s", clip_id);
            curl_easy_setopt(curl, CURLOPT_URL, video_info_query);
			memset(response_buffer->buffer, 0x0, MAX_RESPONSE_BUFFER_SIZE);
			response_buffer->payload_size = 0;
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)response_buffer);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_payload);
            res = curl_easy_perform(curl);
            if(res != CURLE_OK){
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                curl_easy_cleanup(curl);
                curl_global_cleanup();
                return 0;
            }
        }

        {// 1. Find title from the response
			unsigned int actual_title_length;
            char* title_start = strstr(response_buffer->buffer, "title=");
            char* title_end = title_start;
            while((*title_end) != '&' && (*title_end) != 0 && (*title_end) != '\n')
            {
                title_end++;
            }
            int curl_easy_unescape_out;
            curl_title = curl_easy_unescape(curl, title_start+strlen("title="), title_end-title_start-strlen("title="), &curl_easy_unescape_out);
			actual_title_length = strlen(curl_title);
			memset(title, 0x0, title_size);
            strncpy(title, curl_title, (actual_title_length < size-1?actual_title_length:size-1));
			curl_free(curl_title);
        }

        {// 2. Find url_encoded_fmt_stream_map from the response
            char* url_encoded_fmt_stream_map_start = strstr(response_buffer->buffer, "url_encoded_fmt_stream_map=");
            char* url_encoded_fmt_stream_map_end = url_encoded_fmt_stream_map_start;
            while((*url_encoded_fmt_stream_map_end) != '&' && (*url_encoded_fmt_stream_map_end) != 0)
            {
                url_encoded_fmt_stream_map_end++;
            }
            int curl_easy_unescape_out;
            curl_stream_map = curl_easy_unescape(curl,
                                                 url_encoded_fmt_stream_map_start+strlen("url_encoded_fmt_stream_map="),
                                                 url_encoded_fmt_stream_map_end-url_encoded_fmt_stream_map_start-strlen("url_encoded_fmt_stream_map="),
                                                 &curl_easy_unescape_out);
        }
		delete response_buffer;

        {// 3. Find video information from stream_map
            char* video_item = strtok(curl_stream_map, ",");
            while(video_item)
            {
                int curl_easy_unescape_out;
                curl_video_item_list[video_items++] = curl_easy_unescape(curl, video_item, strlen(video_item), &curl_easy_unescape_out);
                video_item = strtok(0, ",");
                if(video_items >= size)
                {
                    break;
                }
            }
        }

        {// 4. Generate request url with curl_video_item_list
            for(unsigned int i = 0 ; i < video_items ; i++)
            {
                video_request_urls[i] = new char[strlen(curl_video_item_list[i])]();
                // find url first.
                char* url_start = strstr(curl_video_item_list[i], "url=");
                if(url_start == 0)
                {
                    continue;
                }
                strcpy(video_request_urls[i], url_start+strlen("url="));
                video_request_urls[i][strlen(video_request_urls[i])] = '&';
                strncpy(video_request_urls[i]+strlen(video_request_urls[i]), curl_video_item_list[i], url_start - curl_video_item_list[i]);
                if(video_request_urls[i][strlen(video_request_urls[i])-1] == '&')
                {
                    video_request_urls[i][strlen(video_request_urls[i])-1] = 0;
                }
            }
        }

        {// 5. Remove duplicate itag field. (I don't know why itag field comes twice. Anyway itag should not appear more then once.)
            for(unsigned int i = 0 ; i < video_items ; i++)
            {
                char* second_itag_start = strstr(video_request_urls[i], "itag=");
                if(second_itag_start == 0)
                {
                    continue;
                }
                second_itag_start = strstr(second_itag_start+1, "&itag=");
                if(second_itag_start == 0)
                {
                    continue;
                }
                char* second_itag_end = strstr(second_itag_start+1, "&");
                second_itag_end = second_itag_end;

                if(second_itag_end)
                {
                    memmove(second_itag_start, second_itag_end, strlen(second_itag_end));
                }
                else
                {
                    memset(second_itag_start, 0x0, strlen(second_itag_start));
                }
            }
        }

        for(unsigned int i = 0 ; i < video_items ; i++)
        {
            curl_free(curl_video_item_list[i]);
        }
        curl_free(curl_stream_map);

        curl_easy_cleanup(curl);
        curl_global_cleanup();
    }
    return video_items;
}

unsigned char get_clip_format(char* url)
{
#define YT_CLIP_TYPE_UNKNOWN 0
#define YT_CLIP_TYPE_WEBM 1
#define YT_CLIP_TYPE_MP4 2
#define YT_CLIP_TYPE_3GPP 3
#define YT_CLIP_TYPE_FLV 4
    char *type = strstr(url, "type=");
    if(type == 0)
    {
        return YT_CLIP_TYPE_UNKNOWN;
    }
    if(strncmp(type+5, "video/webm", 10) == 0)
    {
        return YT_CLIP_TYPE_WEBM;
    }
    else if(strncmp(type+5, "video/mp4", 9) == 0)
    {
        return YT_CLIP_TYPE_MP4;
    }
    else if(strncmp(type+5, "video/3gpp", 10) == 0)
    {
        return YT_CLIP_TYPE_3GPP;
    }
    else if(strncmp(type+5, "video/x-flv", 11) == 0)
    {
        return YT_CLIP_TYPE_FLV;
    }
    else
    {
        return YT_CLIP_TYPE_UNKNOWN;
    }
}

void download(char* url, char* filename_extenstion)
{
    FILE* file;
    CURL *curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if(curl == 0)
    {
        curl_global_cleanup();
        return;
    }

    file = fopen(filename_extenstion, "wb");
    if(file == 0)
    {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return;
    }
    if(file)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)file);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite_payload);
        res = curl_easy_perform(curl);
        if(res != CURLE_OK){
            fclose(file);
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return;
        }
        fclose(file);
    }
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return;
}

int main(int argc, char* argv[])
{
    if(argc != 3)
    {
        printf("Usage: %s url format{webm, mp4, 3gpp, flv}", argv[0]);
        return -1;
    }
    char* video_request_urls[MAX_VIDEO_ITEMS] = {0};
    char title[MAX_TITLE_LENGTH] = {0};
    unsigned int ret = get_urls(argv[1], title, MAX_TITLE_LENGTH, video_request_urls, MAX_VIDEO_ITEMS);
    printf("Got clip download urls\n");
    printf("Title: %s\n", title);
    for(unsigned int i = 0 ; i <ret ; i++)
    {
        unsigned char type = get_clip_format(video_request_urls[i]);
	    printf("Got clip type\n");
        switch(type)
        {
        case YT_CLIP_TYPE_UNKNOWN:
            break;
        case YT_CLIP_TYPE_WEBM:
            if(strcmp(argv[2], "webm") == 0)
            {
                char file_ext[128] = {0};
                sprintf(file_ext, "%s.webm", title);
                printf("Start downloading %s.\n", file_ext);
				printf("URL >> %s\n", video_request_urls[i]);
                download(video_request_urls[i], file_ext);
                printf("Downloading is completed.\n");
                return 0;
            }
            break;
        case YT_CLIP_TYPE_MP4:
            if(strcmp(argv[2], "mp4") == 0)
            {
                char file_ext[128] = {0};
                sprintf(file_ext, "%s.mp4", title);
                printf("Start downloading %s.\n", file_ext);
				printf("URL >> %s\n", video_request_urls[i]);
                download(video_request_urls[i], file_ext);
                printf("Downloading is completed.\n");
                return 0;
            }
            break;
        case YT_CLIP_TYPE_3GPP:
            if(strcmp(argv[2], "3gpp") == 0)
            {
                char file_ext[128] = {0};
                sprintf(file_ext, "%s.3gpp", title);
                printf("Start downloading %s.\n", file_ext);
				printf("URL >> %s\n", video_request_urls[i]);
                download(video_request_urls[i], file_ext);
                printf("Downloading is completed.\n");
                return 0;
            }
            break;
        case YT_CLIP_TYPE_FLV:
            if(strcmp(argv[2], "flv") == 0)
            {
                char file_ext[128] = {0};
                sprintf(file_ext, "%s.flv", title);
                printf("Start downloading %s.\n", file_ext);
				printf("URL >> %s\n", video_request_urls[i]);
                download(video_request_urls[i], file_ext);
                printf("Downloading is completed.\n");
                return 0;
            }
            break;
        }
    }
    return 0;
}
