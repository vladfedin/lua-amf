/*
* load.c: Lua-AMF Lua module code
* Copyright (c) 2010, lua-amf authors
* See copyright information in the COPYRIGHT file
*/

#include <string.h> /* memcpy */

#include "luaheaders.h"
#include "luaamf.h"
#include "saveload.h"
#include "decode.h"

typedef struct amf_LoadState
{
  const unsigned char * pos;
  size_t unread;
} amf_LoadState;

int load_value(
    lua_State * L,
    amf_LoadState * ls,
    int
  );

#define ls_good(ls) \
  ((ls)->pos != NULL)

#define ls_unread(ls) \
  ((ls)->unread)

static unsigned char ls_readbyte(amf_LoadState * ls)
{
  if (ls_good(ls))
  {
    const unsigned char b = * ls->pos;
    ++ls->pos;
    --ls->unread;
    return b;
  }
  return 0;
}

static void ls_init(
    amf_LoadState * ls,
    const unsigned char * data,
    size_t len
  )
{
  ls->pos = (len > 0) ? data : NULL;
  ls->unread = len;
}

int load_int(
    lua_State * L,
    amf_LoadState * ls
  )
{
  int curr_value;
  int result;
  lua_Number value;
  result = decode_int(ls -> pos, &curr_value);
  value = curr_value;
  if (result == LUAAMF_ESUCCESS)
  {
    lua_pushnumber(L, value);
  }
  return result;
}

int load_double(
    lua_State * L,
    amf_LoadState * ls
  )
{
  double c_value;
  int result;
  lua_Number value;
  result = decode_double(ls -> pos, &c_value);
  value = c_value;
  if (result == LUAAMF_ESUCCESS)
  {
    lua_pushnumber(L, value);
    ls -> pos = ls -> pos + 8;
    ls -> unread = ls -> unread - 8;
  }
  return result;
}

int load_string(
    lua_State * L,
    amf_LoadState * ls,
    int check_size
  )
{
  int result;
  unsigned int value = 0;
  unsigned int byte_cnt = 0;
  char byte;
  if (ls -> pos == NULL)
  {
    return LUAAMF_EBADDATA;
  }
  byte = ls -> pos[0];

  /* If 0x80 is set, int includes the next byte, up to 4 total bytes */
  while ((byte & 0x80) && (byte_cnt < 3))
  {
      value <<= 7;
      value |= byte & 0x7F;
      byte = ls -> pos[byte_cnt + 1];
      byte_cnt++;
  }

  /* shift bits in last byte */
  if (byte_cnt < 3)
  {
      /* shift by 7, since the 1st bit is reserved for next byte flag */
      value <<= 7;
      value |= byte & 0x7F;
  }
  else
  {
      /* shift by 8, since no further bytes are
         possible and 1st bit is not used for flag. */
      value <<= 8;
      value |= byte & 0xff;
  }

  /* Move sign bit, since we're converting 29bit->32bit */
  if (value & 0x10000000) { value -= 0x20000000; }
  if (value != ((ls -> unread  - byte_cnt) * 2 - 1) && check_size)
  {
    result = LUAAMF_EBADSIZE;
  }
  else
  {
    lua_pushlstring(
        L,
        (const char *)(ls -> pos + byte_cnt + 1),
        (value + 1) / 2 - 1
      );
    ls -> pos = ls -> pos + (value + 1) / 2;
    ls -> unread = ls -> unread - (value + 1) / 2;
    result = LUAAMF_ESUCCESS;
  }
  return result;
}

int load_array(
    lua_State * L,
    amf_LoadState * ls,
    int check_size
  )
{
  int result;
  int number = 0;
  int index = 0;

  lua_newtable(L);
  {
    int curr_value;
    result = decode_int(ls -> pos, &curr_value);
    if (result == LUAAMF_ESUCCESS) {
      number = (curr_value - 1) / 2;
    }
  }

  --ls -> unread;
  ++ls -> pos;

  for (; number > 0; number--)
  {
    load_string(L, ls, 0);
    load_value(L, ls, 0);
    lua_settable(L, -3);
  }
  if (ls_readbyte(ls) != LUAAMF_NULL)
    return LUAAMF_EBADSIZE;

  while (ls -> unread != 0)
  {
    index++;
    lua_pushinteger(L, index);
    load_value(L, ls, 0);
    lua_settable(L, -3);
  }

  result = LUAAMF_ESUCCESS;
  return result;
}

int load_value(
    lua_State * L,
    amf_LoadState * ls,
    int check_size
  )
{
  int result = LUAAMF_EFAILURE;
  unsigned char type;
  type = ls_readbyte(ls);

  switch (type)
  {
  case LUAAMF_NULL:
    if(ls -> unread != 0 && check_size)
    {
      result = LUAAMF_ETAILEFT;
    }
    else
    {
      lua_pushnil(L);
      result = LUAAMF_ESUCCESS;
    }
    break;

  case LUAAMF_FALSE:
    if(ls -> unread != 0 && check_size)
    {
      result = LUAAMF_ETAILEFT;
    }
    else
    {
      lua_pushboolean(L, 0);
      result = LUAAMF_ESUCCESS;
    }
    break;

  case LUAAMF_TRUE:
    if(ls -> unread != 0 && check_size)
    {
      result = LUAAMF_ETAILEFT;
    }
    else
    {
      lua_pushboolean(L, 1);
      result = LUAAMF_ESUCCESS;
    }
    break;

  case LUAAMF_INT:
    if(ls -> unread > 5 && check_size)
    {
      result = LUAAMF_ETAILEFT;
    }
    else
    {
      result = load_int(L, ls);
    }
    break;

  case LUAAMF_DOUBLE:
    if(ls -> unread != 8 && check_size)
    {
      result = LUAAMF_ETAILEFT;
    }
    else
    {
      result = load_double(L, ls);
    }
    break;

  case LUAAMF_STRING:
    result = load_string(L, ls, check_size);
    break;

  case LUAAMF_ARRAY:
    result = load_array(L, ls, check_size);
    break;

  default:
    result = LUAAMF_EBADTYPE;
    break;
  }

  return result;
}

int luaamf_load(
    lua_State * L,
    const unsigned char * data,
    size_t len
  )
{
  amf_LoadState ls;
  int result = LUAAMF_EFAILURE;
  ls_init(&ls, data, len);
  if (!ls_good(&ls))
  {
    result = LUAAMF_EBADDATA;
  }
  else
  {
    result = load_value(L, &ls, 1);
  }

  if (result != LUAAMF_ESUCCESS)
  {
    lua_settop(L, lua_gettop(L)); /* Discard intermediate results */
    switch (result)
    {
    case LUAAMF_EBADTYPE:
      lua_pushliteral(L, "can't load: bad data type");
      break;

    case LUAAMF_EBADDATA:
      lua_pushliteral(L, "can't load: corrupt data");
      break;

    case LUAAMF_EBADSIZE:
      lua_pushliteral(L, "can't load: corrupt data, bad size");
      break;

    case LUAAMF_ETAILEFT:
      lua_pushliteral(L, "can't load: extra data at end");
      break;

    default: /* Should not happen */
      lua_pushliteral(L, "load failed");
      break;
    }
  }

  return result;
}
