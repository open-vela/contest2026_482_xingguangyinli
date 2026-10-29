#include "lc_product_transport.h"
#include "lc_product_trust.h"
#include "lc_strict_https.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char session[] = "meal_0123456789ab";
static const char device_token[] = "lc-owner-device-token-0123456789abcdef";
static const char phrase[] = "一个人，三十元，微辣，不吃香菜。";
static const char *expected_text = phrase;
static const char *reply = "{\"session_id\":\"meal_0123456789ab\",\"recommendation\":{\"candidate\":{\"name\":\"番茄牛肉饭\",\"price_cny\":30},\"reason\":\"满足当次要求\",\"source\":\"model\",\"rules_fallback\":false},\"requires_handoff\":true,\"spoken_reply\":\"想试试这道吗？\"}";
static int calls, session_calls, confirm_calls;
static int http_status = 200;
static bool allow_new_session;
static bool allow_confirmation;
static bool expect_rejection;
static const unsigned char *binary_reply;
static size_t binary_reply_length;
static const char *confirm_reply =
  "{\"compact_path\":\"/c/0123456789ab\"}";

int lc_strict_https_request(const char *host, const char *port,
  const char *method, const char *path, const lc_private_http_header_t *headers,
  const void *raw_body, size_t body_length, const char *ca_pem,
  size_t ca_length, void *raw_response, size_t capacity, size_t *received)
{
  const char *body = raw_body;
  char *response = raw_response;
  calls++;
  assert(strcmp(host,"example.test")==0 && strcmp(port,"443")==0);
  assert(strcmp(method,"POST")==0);
  assert(strcmp(headers[0].name,"Content-Type")==0);
  assert(strcmp(headers[0].value,"application/json")==0);
  assert(strcmp(headers[1].name,"Authorization")==0);
  assert(strcmp(headers[1].value,"Bearer lc-owner-device-token-0123456789abcdef")==0);
  assert(headers[2].name==NULL);
  assert(ca_pem==lc_product_trust_anchor_pem && ca_length==lc_product_trust_anchor_pem_size);
  assert(body_length==strlen(body));
  cJSON *json=cJSON_Parse(body);
  assert(cJSON_IsObject(json));
  const char *output=reply;
  int result=http_status;
  if(strcmp(path,"/api/v1/session")==0)
    {
      assert(allow_new_session); /* ASR reuse must NEVER create another one. */
      session_calls++;
      output="{\"session_id\":\"meal_0123456789ab\"}";
      result=201;
    }
  else
    {
      cJSON *id=cJSON_GetObjectItemCaseSensitive(json,"session_id");
      assert(cJSON_IsString(id) && strcmp(id->valuestring,session)==0);
      if(strcmp(path,"/api/v1/dialogue")==0)
        {
          cJSON *text=cJSON_GetObjectItemCaseSensitive(json,"transcript");
          assert(cJSON_IsString(text) && strcmp(text->valuestring,expected_text)==0);
          cJSON *choice=cJSON_GetObjectItemCaseSensitive(json,"choice");
          assert(cJSON_IsString(choice) && strcmp(choice->valuestring,"takeout")==0);
          cJSON *reject=cJSON_GetObjectItemCaseSensitive(json,"reject_current");
          assert(cJSON_IsTrue(reject)==expect_rejection);
        }
      else
        {
          assert(allow_confirmation);
          confirm_calls++;
          if(strcmp(path,"/api/v1/device/event")==0)
            {
              cJSON *event=cJSON_GetObjectItemCaseSensitive(json,"event");
              assert(cJSON_IsString(event) && strcmp(event->valuestring,"accept_candidate")==0);
              output="{\"state\":\"confirming\"}";
            }
          else
            {
              assert(strcmp(path,"/api/v1/confirm")==0);
              cJSON *platform=cJSON_GetObjectItemCaseSensitive(json,"platform");
              assert(cJSON_IsString(platform) && strcmp(platform->valuestring,"eleme")==0);
              output=confirm_reply;
            }
        }
    }
  cJSON_Delete(json);
  if(binary_reply!=NULL)
    {
      assert(binary_reply_length<capacity);
      memcpy(response,binary_reply,binary_reply_length);
      *received=binary_reply_length;
      return result;
    }
  size_t n=strlen(output);
  assert(n<capacity);
  memcpy(response,output,n+1u);
  *received=n;
  return result;
}

int main(void)
{
  lc_product_transport_t backend;
  lc_product_result_t result;
  assert(lc_product_transport_init_authenticated(
    &backend,"https://example.test/api",device_token));
  assert(lc_product_transport_recommend_session(&backend,session,LC_CHOICE_TAKEOUT,
    phrase,&result)==LC_PRODUCT_TRANSPORT_OK);
  assert(calls==1 && session_calls==0 && confirm_calls==0);
  assert(strcmp(result.name,"番茄牛肉饭")==0);
  assert(strcmp(result.price,"¥30")==0);
  assert(result.requires_handoff && result.source==LC_PRODUCT_SOURCE_MODEL);
  expect_rejection=true;
  assert(lc_product_transport_reject_session(&backend,session,LC_CHOICE_TAKEOUT,
    phrase,&result)==LC_PRODUCT_TRANSPORT_OK);
  assert(session_calls==0 && confirm_calls==0);
  expect_rejection=false;

  /* Preserve all 500 four-byte UTF8 characters, no 256-byte truncation. */
  char long_text[2001];
  for(size_t i=0;i<500u;i++) memcpy(long_text+i*4u,"𠮷",4u);
  long_text[2000]='\0';
  expected_text=long_text;
  assert(lc_product_transport_recommend_session(&backend,session,LC_CHOICE_TAKEOUT,
    long_text,&result)==LC_PRODUCT_TRANSPORT_OK);
  assert(session_calls==0 && confirm_calls==0);
  expected_text=phrase;
  int previous=calls;
  const char *invalid_id[]={NULL,"","meal_bad&id=x","meal_0123456789abc","meal_0123456789AG"};
  for(size_t i=0;i<sizeof(invalid_id)/sizeof(invalid_id[0]);i++)
    assert(lc_product_transport_recommend_session(&backend,invalid_id[i],LC_CHOICE_TAKEOUT,
      phrase,&result)==LC_PRODUCT_TRANSPORT_INVALID);
  assert(lc_product_transport_recommend_session(&backend,session,(lc_choice_id_t)99,
    phrase,&result)==LC_PRODUCT_TRANSPORT_INVALID);
  assert(lc_product_transport_recommend_session(&backend,session,LC_CHOICE_TAKEOUT,
    "",&result)==LC_PRODUCT_TRANSPORT_INVALID);
  assert(calls==previous);
  const char *good=reply;
  reply="{\"session_id\":\"meal_ffffffffffff\",\"recommendation\":{\"candidate\":{\"name\":\"错误会话的菜\",\"price_cny\":30},\"reason\":\"不应显示\",\"source\":\"model\"},\"requires_handoff\":true,\"spoken_reply\":\"不应播报\"}";
  assert(lc_product_transport_recommend_session(&backend,session,LC_CHOICE_TAKEOUT,
    phrase,&result)==LC_PRODUCT_TRANSPORT_SCHEMA);
  assert(result.name[0]=='\0' && result.spoken_reply[0]=='\0');
  reply=good;
  {
    unsigned char poisoned[1024];
    size_t good_length=strlen(good);
    assert(good_length+5u<sizeof(poisoned));
    memcpy(poisoned,good,good_length);
    poisoned[good_length]='\0';
    memcpy(poisoned+good_length+1u,"JUNK",4u);
    binary_reply=poisoned;
    binary_reply_length=good_length+5u;
    assert(lc_product_transport_recommend_session(
      &backend,session,LC_CHOICE_TAKEOUT,phrase,&result)==
      LC_PRODUCT_TRANSPORT_SCHEMA);
    assert(result.name[0]=='\0' && result.spoken_reply[0]=='\0');
    binary_reply=NULL;
    binary_reply_length=0u;
  }
  {
    unsigned char too_deep[1024];
    size_t offset=0u;
    const char prefix[]="{\"padding\":";
    memcpy(too_deep+offset,prefix,sizeof(prefix)-1u);
    offset+=sizeof(prefix)-1u;
    for(size_t i=0u;i<17u;i++) too_deep[offset++]='[';
    too_deep[offset++]='0';
    for(size_t i=0u;i<17u;i++) too_deep[offset++]=']';
    too_deep[offset++]=',';
    memcpy(too_deep+offset,good+1u,strlen(good)-1u);
    offset+=strlen(good)-1u;
    binary_reply=too_deep;
    binary_reply_length=offset;
    assert(lc_product_transport_recommend_session(
      &backend,session,LC_CHOICE_TAKEOUT,phrase,&result)==
      LC_PRODUCT_TRANSPORT_SCHEMA);
    assert(result.name[0]=='\0' && result.spoken_reply[0]=='\0');
    binary_reply=NULL;
    binary_reply_length=0u;
  }
  const int failures[]={302,401,503,-1};
  for(size_t i=0;i<sizeof(failures)/sizeof(failures[0]);i++)
    {
      http_status=failures[i];previous=calls;
      assert(lc_product_transport_recommend_session(&backend,session,LC_CHOICE_TAKEOUT,
        phrase,&result)==(http_status<0?LC_PRODUCT_TRANSPORT_NETWORK:LC_PRODUCT_TRANSPORT_HTTP));
      assert(calls==previous+1 && result.name[0]=='\0');
    }
  http_status=200;
  expect_rejection=true;
  reply="{\"detail\":\"no_safe_candidate\"}";
  http_status=409; previous=calls;
  assert(lc_product_transport_reject_session(&backend,session,LC_CHOICE_TAKEOUT,
    phrase,&result)==LC_PRODUCT_TRANSPORT_NO_CANDIDATE);
  assert(calls==previous+1 && result.name[0]=='\0' && result.spoken_reply[0]=='\0');
  reply="{\"detail\":\"candidate_required\"}";
  assert(lc_product_transport_reject_session(&backend,session,LC_CHOICE_TAKEOUT,
    phrase,&result)==LC_PRODUCT_TRANSPORT_HTTP);
  reply=good; http_status=200; expect_rejection=false;
  /* Touch-only legacy entry still creates exactly one session. */
  allow_new_session=true;
  char id[LC_PRODUCT_SESSION_MAX];
  previous=calls;
  assert(lc_product_transport_recommend(&backend,LC_CHOICE_TAKEOUT,phrase,&result,
    id,sizeof(id))==LC_PRODUCT_TRANSPORT_OK);
  assert(calls==previous+2 && session_calls==1 && strcmp(id,session)==0);
  allow_new_session=false;
  assert(confirm_calls==0);
  allow_confirmation=true;
  char url[256];
  assert(lc_product_transport_confirm(&backend,session,url,sizeof(url))==LC_PRODUCT_TRANSPORT_OK);
  assert(confirm_calls==2 && strcmp(url,"https://example.test/api/c/0123456789ab")==0);
  confirm_reply="{\"compact_path\":\"/c/ffffffffffff\"}";
  strcpy(url,"stale-qr-must-be-cleared");
  assert(lc_product_transport_confirm(&backend,session,url,sizeof(url))==
         LC_PRODUCT_TRANSPORT_SCHEMA);
  assert(url[0]=='\0');
  confirm_reply="{\"compact_path\":\"//evil.example/c/0123456789ab\"}";
  strcpy(url,"stale-qr-must-be-cleared");
  assert(lc_product_transport_confirm(&backend,session,url,sizeof(url))==
         LC_PRODUCT_TRANSPORT_SCHEMA);
  assert(url[0]=='\0');
  puts("PASS: native dialogue reuses ASR session/complete UTF8/matching response/Chinese price/no implicit confirmation");
  return 0;
}
