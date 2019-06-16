# This is a builtin machine that checks values in the datasource
# whenever the key is set.
#
# The data source may provide validators that can be used to verify that the
# item loaded meets certain criteria.

KEYVALUE MACHINE {
  PLUGIN "key_value.so.1.0";
  OPTION Key "";
  OPTION Value "";
  
  Empty STATE;
  Invalid STATE;
  Ready STATE;
  
  COMMAND clear { Key := ""; }

%BEGIN_PLUGIN
#include <stdlib.h>
#include <Plugin.h>
#include <string.h>
#include <hiredis/hiredis.h>

struct MyData {
  redisContext *redis;
  char * key;
  char *value;
};

struct MyData *init(cwpi_Scope scope) {
  struct MyData *data = (struct MyData*)getInstanceData(scope);
	if (!data) { 
    data = malloc(sizeof(struct MyData));
    memset(data, 0, sizeof(struct MyData));
    setInstanceData(scope, data);
  }
  if (data && !data->redis) {
    data->redis = redisConnect("localhost", 6379);
    if (!data->redis) {
      printf("failed to connect to redis\n");
    }
    else if (data->redis->err) {
      printf("Redis connection error %s\n", data->redis->errstr);
      data->redis = 0;
    }
  }
  return data;
}

int update(cwpi_Scope scope) {
  struct MyData *data = (struct MyData*)getInstanceData(scope);
  if (!data || !data->redis) {
    data = init(data);
  }
  if (data && data->redis) {
    char *key = getStringValue(scope, "Key");
    char *value = getStringValue(scope, "Value");
    if (!data->key || strcmp(data->key, key)) {
      // changed key
      if (data->key) free(data->key);
      data->key = key;
      if (data->value) {
        free(data->value); data->value = 0;
        free(value); value = 0;
      }
      // lookup
      if (data->key[0] == 0) {
        data->value = strdup("");
        setStringValue(scope, "Value", data->value);
        changeState(scope, "Empty");
      }
      else {
        printf("looking up %s\n", data->key);
        redisReply *reply = redisCommand(data->redis, "GET %s", data->key);
        if (reply) {
          if (data->redis->err) {
            printf("redis error: %s\n", data->redis->errstr);
            changeState(scope, "Invalid");
          }
          else if (reply->str) {
            data->value = strdup(reply->str);
            setStringValue(scope, "Value", data->value);
            changeState(scope, "Ready");
          }
          else {
            data->value = strdup("");
            setStringValue(scope, "Value", data->value);
            if (data->key[0] == 0)
              changeState(scope, "Empty");
            else
              changeState(scope, "Invalid");
          }
          freeReplyObject(reply);
        }
        else {
          changeState(scope, "Invalid");
        }
      }
    }
    else if (!data->value || strcmp(data->value, value)) {
      // value changed
      if (data->value) free(data->value);
      data->value = value;
      redisReply *reply = redisCommand(data->redis, "SET \"%s\" \"%s\"", data->key, data->value);
      freeReplyObject(reply);
    }
  }
	return PLUGIN_COMPLETED;
}

PLUGIN_EXPORT
int check_states(cwpi_Scope scope)
{
  struct MyData *data = getInstanceData(scope);
  if (data && data->redis) return update(scope);
  return PLUGIN_COMPLETED;
}

PLUGIN_EXPORT
int poll_actions(cwpi_Scope scope) {
  struct MyData *data = getInstanceData(scope);
  if (!data || !data->redis) {
    printf("poll_actions calling init\n");
    data = init(scope);
    printf("init done\n");
  }
  return PLUGIN_COMPLETED;
}

%END_PLUGIN

}
