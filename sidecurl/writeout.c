// Copied from curl/src/tool_writeout.c
/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) 1998 - 2022, Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 * SPDX-License-Identifier: curl
 *
 ***************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include "curl/curl.h"

#define unsupported(option) \
    do { \
        fprintf(stderr, "sidecurl doesn't support '%s'...yet...complain to Matthew\n", option); \
        exit(1); \
    } while(0)

typedef enum {
  VAR_NONE,       /* must be the first */
  VAR_APPCONNECT_TIME,
  VAR_CONNECT_TIME,
  VAR_CONTENT_TYPE,
  VAR_EFFECTIVE_FILENAME,
  VAR_EFFECTIVE_METHOD,
  VAR_EFFECTIVE_URL,
  VAR_ERRORMSG,
  VAR_EXITCODE,
  VAR_FTP_ENTRY_PATH,
  VAR_HEADER_JSON,
  VAR_HEADER_SIZE,
  VAR_HTTP_CODE,
  VAR_HTTP_CODE_PROXY,
  VAR_HTTP_VERSION,
  VAR_INPUT_URL,
  VAR_JSON,
  VAR_LOCAL_IP,
  VAR_LOCAL_PORT,
  VAR_NAMELOOKUP_TIME,
  VAR_NUM_CONNECTS,
  VAR_NUM_HEADERS,
  VAR_ONERROR,
  VAR_PRETRANSFER_TIME,
  VAR_PRIMARY_IP,
  VAR_PRIMARY_PORT,
  VAR_PROXY_SSL_VERIFY_RESULT,
  VAR_REDIRECT_COUNT,
  VAR_REDIRECT_TIME,
  VAR_REDIRECT_URL,
  VAR_REFERER,
  VAR_REQUEST_SIZE,
  VAR_SCHEME,
  VAR_SIZE_DOWNLOAD,
  VAR_SIZE_UPLOAD,
  VAR_SPEED_DOWNLOAD,
  VAR_SPEED_UPLOAD,
  VAR_SSL_VERIFY_RESULT,
  VAR_STARTTRANSFER_TIME,
  VAR_STDERR,
  VAR_STDOUT,
  VAR_TOTAL_TIME,
  VAR_URLNUM,
  VAR_NUM_OF_VARS /* must be the last */
} writeoutid;

struct writeoutvar {
  const char *name;
  writeoutid id;
  CURLINFO ci;
  int (*writefunc)(FILE *stream, const struct writeoutvar *wovar,
                   CURL *easy, CURLcode per_result,
                   bool use_json);
};

static int writeTime(FILE *stream, const struct writeoutvar *wovar,
                     CURL *easy, CURLcode per_result,
                     bool use_json);

static int writeString(FILE *stream, const struct writeoutvar *wovar,
                       CURL *easy, CURLcode per_result,
                       bool use_json);

static int writeLong(FILE *stream, const struct writeoutvar *wovar,
                     CURL *easy, CURLcode per_result,
                     bool use_json);

static int writeOffset(FILE *stream, const struct writeoutvar *wovar,
                       CURL *easy, CURLcode per_result,
                       bool use_json);

void ourWriteOut(const char *writeinfo, CURL *easy,
                 CURLcode per_result);

struct httpmap {
  const char *str;
  int num;
};

static const struct httpmap http_version[] = {
  { "0",   CURL_HTTP_VERSION_NONE},
  { "1",   CURL_HTTP_VERSION_1_0},
  { "1.1", CURL_HTTP_VERSION_1_1},
  { "2",   CURL_HTTP_VERSION_2},
  { "3",   CURL_HTTP_VERSION_3},
  { NULL, 0} /* end of list */
};

/* The designated write function should be the same as the CURLINFO return type
   with exceptions special cased in the respective function. For example,
   http_version uses CURLINFO_HTTP_VERSION which returns the version as a long,
   however it is output as a string and therefore is handled in writeString.

   Yes: "http_version": "1.1"
   No:  "http_version": 1.1

   Variable names should be in alphabetical order.
   */
static const struct writeoutvar variables[] = {
  {"content_type", VAR_CONTENT_TYPE, CURLINFO_CONTENT_TYPE, writeString},
  {"errormsg", VAR_ERRORMSG, CURLINFO_NONE, writeString},
  {"exitcode", VAR_EXITCODE, CURLINFO_NONE, writeLong},
  {"filename_effective", VAR_EFFECTIVE_FILENAME, CURLINFO_NONE, writeString},
  {"ftp_entry_path", VAR_FTP_ENTRY_PATH, CURLINFO_FTP_ENTRY_PATH, writeString},
  {"header_json", VAR_HEADER_JSON, CURLINFO_NONE, NULL},
  {"http_code", VAR_HTTP_CODE, CURLINFO_RESPONSE_CODE, writeLong},
  {"http_connect", VAR_HTTP_CODE_PROXY, CURLINFO_HTTP_CONNECTCODE, writeLong},
  {"http_version", VAR_HTTP_VERSION, CURLINFO_HTTP_VERSION, writeString},
  {"json", VAR_JSON, CURLINFO_NONE, NULL},
  {"local_ip", VAR_LOCAL_IP, CURLINFO_LOCAL_IP, writeString},
  {"local_port", VAR_LOCAL_PORT, CURLINFO_LOCAL_PORT, writeLong},
  {"method", VAR_EFFECTIVE_METHOD, CURLINFO_EFFECTIVE_METHOD, writeString},
  {"num_connects", VAR_NUM_CONNECTS, CURLINFO_NUM_CONNECTS, writeLong},
  {"num_headers", VAR_NUM_HEADERS, CURLINFO_NONE, writeLong},
  {"num_redirects", VAR_REDIRECT_COUNT, CURLINFO_REDIRECT_COUNT, writeLong},
  {"onerror", VAR_ONERROR, CURLINFO_NONE, NULL},
  {"proxy_ssl_verify_result", VAR_PROXY_SSL_VERIFY_RESULT,
   CURLINFO_PROXY_SSL_VERIFYRESULT, writeLong},
  {"redirect_url", VAR_REDIRECT_URL, CURLINFO_REDIRECT_URL, writeString},
  {"referer", VAR_REFERER, CURLINFO_REFERER, writeString},
  {"remote_ip", VAR_PRIMARY_IP, CURLINFO_PRIMARY_IP, writeString},
  {"remote_port", VAR_PRIMARY_PORT, CURLINFO_PRIMARY_PORT, writeLong},
  {"response_code", VAR_HTTP_CODE, CURLINFO_RESPONSE_CODE, writeLong},
  {"scheme", VAR_SCHEME, CURLINFO_SCHEME, writeString},
  {"size_download", VAR_SIZE_DOWNLOAD, CURLINFO_SIZE_DOWNLOAD_T, writeOffset},
  {"size_header", VAR_HEADER_SIZE, CURLINFO_HEADER_SIZE, writeLong},
  {"size_request", VAR_REQUEST_SIZE, CURLINFO_REQUEST_SIZE, writeLong},
  {"size_upload", VAR_SIZE_UPLOAD, CURLINFO_SIZE_UPLOAD_T, writeOffset},
  {"speed_download", VAR_SPEED_DOWNLOAD, CURLINFO_SPEED_DOWNLOAD_T,
   writeOffset},
  {"speed_upload", VAR_SPEED_UPLOAD, CURLINFO_SPEED_UPLOAD_T, writeOffset},
  {"ssl_verify_result", VAR_SSL_VERIFY_RESULT, CURLINFO_SSL_VERIFYRESULT,
   writeLong},
  {"stderr", VAR_STDERR, CURLINFO_NONE, NULL},
  {"stdout", VAR_STDOUT, CURLINFO_NONE, NULL},
  {"time_appconnect", VAR_APPCONNECT_TIME, CURLINFO_APPCONNECT_TIME_T,
   writeTime},
  {"time_connect", VAR_CONNECT_TIME, CURLINFO_CONNECT_TIME_T, writeTime},
  {"time_namelookup", VAR_NAMELOOKUP_TIME, CURLINFO_NAMELOOKUP_TIME_T,
   writeTime},
  {"time_pretransfer", VAR_PRETRANSFER_TIME, CURLINFO_PRETRANSFER_TIME_T,
   writeTime},
  {"time_redirect", VAR_REDIRECT_TIME, CURLINFO_REDIRECT_TIME_T, writeTime},
  {"time_starttransfer", VAR_STARTTRANSFER_TIME, CURLINFO_STARTTRANSFER_TIME_T,
   writeTime},
  {"time_total", VAR_TOTAL_TIME, CURLINFO_TOTAL_TIME_T, writeTime},
  {"url", VAR_INPUT_URL, CURLINFO_NONE, writeString},
  {"url_effective", VAR_EFFECTIVE_URL, CURLINFO_EFFECTIVE_URL, writeString},
  {"urlnum", VAR_URLNUM, CURLINFO_NONE, writeLong},
  {NULL, VAR_NONE, CURLINFO_NONE, NULL}
};

static int writeTime(FILE *stream, const struct writeoutvar *wovar,
                     CURL *easy, CURLcode per_result,
                     bool use_json)
{
  bool valid = 0;
  curl_off_t us = 0;

  (void)easy;
  (void)per_result;
  assert(wovar->writefunc == writeTime);

  if(wovar->ci) {
    if(!curl_easy_getinfo(easy, wovar->ci, &us))
      valid = true;
  }
  else {
    assert(0);
  }

  if(valid) {
    curl_off_t secs = us / 1000000;
    us %= 1000000;

    if(use_json)
      fprintf(stream, "\"%s\":", wovar->name);

    fprintf(stream, "%" CURL_FORMAT_CURL_OFF_TU
            ".%06" CURL_FORMAT_CURL_OFF_TU, secs, us);
  }
  else {
    if(use_json)
      fprintf(stream, "\"%s\":null", wovar->name);
  }

  return 1; /* return 1 if anything was written */
}

static int writeString(FILE *stream, const struct writeoutvar *wovar,
                       CURL *easy, CURLcode per_result,
                       bool use_json)
{
  bool valid = 0;
  const char *strinfo = NULL;

  assert(wovar->writefunc == writeString);

  if(wovar->ci) {
    if(wovar->ci == CURLINFO_HTTP_VERSION) {
      long version = 0;
      if(!curl_easy_getinfo(easy, CURLINFO_HTTP_VERSION, &version)) {
        const struct httpmap *m = &http_version[0];
        while(m->str) {
          if(m->num == version) {
            strinfo = m->str;
            valid = true;
            break;
          }
          m++;
        }
      }
    }
    else {
      if(!curl_easy_getinfo(easy, wovar->ci, &strinfo) && strinfo)
        valid = true;
    }
  }
  else {
    switch(wovar->id) {
    case VAR_ERRORMSG:
        unsupported("%{errormsg}");
      // if(per_result) {
      //   strinfo = (per->errorbuffer && per->errorbuffer[0]) ?
      //     per->errorbuffer : curl_easy_strerror(per_result);
      //   valid = true;
      // }
      break;
    case VAR_EFFECTIVE_FILENAME:
        unsupported("%{filename_effective}");
      // if(per->outs.filename) {
      //   strinfo = per->outs.filename;
      //   valid = true;
      // }
      break;
    case VAR_INPUT_URL:
        unsupported("%{url}");
      // if(per->this_url) {
      //   strinfo = per->this_url;
      //   valid = true;
      // }
      break;
    default:
      assert(0);
      break;
    }
  }

  if(valid) {
    assert(strinfo);
    if(use_json) {
      fprintf(stream, "\"%s\":", wovar->name);
      unsupported("json");
      // jsonWriteString(stream, strinfo, 0);
    }
    else
      fputs(strinfo, stream);
  }
  else {
    if(use_json)
      fprintf(stream, "\"%s\":null", wovar->name);
  }

  return 1; /* return 1 if anything was written */
}

static int writeLong(FILE *stream, const struct writeoutvar *wovar,
                     CURL *easy, CURLcode per_result,
                     bool use_json)
{
  bool valid = 0;
  long longinfo = 0;

  assert(wovar->writefunc == writeLong);

  if(wovar->ci) {
    if(!curl_easy_getinfo(easy, wovar->ci, &longinfo))
      valid = true;
  }
  else {
    switch(wovar->id) {
    case VAR_NUM_HEADERS:
        unsupported("%{num_headers}");
      // longinfo = per->num_headers;
      valid = true;
      break;
    case VAR_EXITCODE:
      longinfo = per_result;
      valid = true;
      break;
    case VAR_URLNUM:
        unsupported("%{urlnum}");
      // if(per->urlnum <= INT_MAX) {
      //   longinfo = (long)per->urlnum;
      //   valid = true;
      // }
      break;
    default:
      assert(0);
      break;
    }
  }

  if(valid) {
    if(use_json)
      fprintf(stream, "\"%s\":%ld", wovar->name, longinfo);
    else {
      if(wovar->id == VAR_HTTP_CODE || wovar->id == VAR_HTTP_CODE_PROXY)
        fprintf(stream, "%03ld", longinfo);
      else
        fprintf(stream, "%ld", longinfo);
    }
  }
  else {
    if(use_json)
      fprintf(stream, "\"%s\":null", wovar->name);
  }

  return 1; /* return 1 if anything was written */
}

static int writeOffset(FILE *stream, const struct writeoutvar *wovar,
                       CURL *easy, CURLcode per_result,
                       bool use_json)
{
  bool valid = 0;
  curl_off_t offinfo = 0;

  (void)easy;
  (void)per_result;
  assert(wovar->writefunc == writeOffset);

  if(wovar->ci) {
    if(!curl_easy_getinfo(easy, wovar->ci, &offinfo))
      valid = true;
  }
  else {
    assert(0);
  }

  if(valid) {
    if(use_json)
      fprintf(stream, "\"%s\":", wovar->name);

    fprintf(stream, "%" CURL_FORMAT_CURL_OFF_T, offinfo);
  }
  else {
    if(use_json)
      fprintf(stream, "\"%s\":null", wovar->name);
  }

  return 1; /* return 1 if anything was written */
}

void ourWriteOut(const char *writeinfo, CURL *easy,
                 CURLcode per_result)
{
  FILE *stream = stdout;
  const char *ptr = writeinfo;
  bool done = 0;

  while(ptr && *ptr && !done) {
    if('%' == *ptr && ptr[1]) {
      if('%' == ptr[1]) {
        /* an escaped %-letter */
        fputc('%', stream);
        ptr += 2;
      }
      else {
        /* this is meant as a variable to output */
        char *end;
        size_t vlen;
        if('{' == ptr[1]) {
          int i;
          bool match = 0;
          end = strchr(ptr, '}');
          ptr += 2; /* pass the % and the { */
          if(!end) {
            fputs("%{", stream);
            continue;
          }
          vlen = end - ptr;
          for(i = 0; variables[i].name; i++) {
            if((strlen(variables[i].name) == vlen) &&
               curl_strnequal(ptr, variables[i].name, vlen)) {
              match = 1;
              switch(variables[i].id) {
              case VAR_ONERROR:
                if(per_result == CURLE_OK)
                  /* this isn't error so skip the rest */
                  done = 1;
                break;
              case VAR_STDOUT:
                stream = stdout;
                break;
              case VAR_STDERR:
                stream = stderr;
                break;
              case VAR_JSON:
                unsupported("%{json}");
              case VAR_HEADER_JSON:
                unsupported("%{header_json}");
              default:
                (void)variables[i].writefunc(stream, &variables[i],
                                             easy, per_result, 0);
                break;
              }
              break;
            }
          }
          if(!match) {
            fprintf(stderr, "curl: unknown --write-out variable: '%.*s'\n",
                    (int)vlen, ptr);
          }
          ptr = end + 1; /* pass the end */
        }
        else if(!strncmp("header{", &ptr[1], 7)) {
          ptr += 8;
          end = strchr(ptr, '}');
          if(end) {
            char hname[256]; /* holds the longest header field name */
            struct curl_header *header;
            vlen = end - ptr;
            if(vlen < sizeof(hname)) {
              memcpy(hname, ptr, vlen);
              hname[vlen] = 0;
              if(CURLHE_OK == curl_easy_header(easy, hname, 0,
                                               CURLH_HEADER, -1, &header))
                fputs(header->value, stream);
            }
            ptr = end + 1;
          }
          else
            fputs("%header{", stream);
        }
        else {
          /* illegal syntax, then just output the characters that are used */
          fputc('%', stream);
          fputc(ptr[1], stream);
          ptr += 2;
        }
      }
    }
    else if('\\' == *ptr && ptr[1]) {
      switch(ptr[1]) {
      case 'r':
        fputc('\r', stream);
        break;
      case 'n':
        fputc('\n', stream);
        break;
      case 't':
        fputc('\t', stream);
        break;
      default:
        /* unknown, just output this */
        fputc(*ptr, stream);
        fputc(ptr[1], stream);
        break;
      }
      ptr += 2;
    }
    else {
      fputc(*ptr, stream);
      ptr++;
    }
  }
}
