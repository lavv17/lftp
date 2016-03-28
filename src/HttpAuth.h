/*
 * lftp - file transfer program
 *
 * Copyright (c) 2016 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HTTPAUTH_H
#define HTTPAUTH_H

#include "xmap.h"
#include "xarray.h"
#include "HttpHeader.h"

class HttpAuth
{
public:
   enum target_t { WWW=0, PROXY };
   enum scheme_t { NONE=0, BASIC, DIGEST };

   class Challenge
   {
      scheme_t scheme_code;
      xstring scheme;
      xmap_p<xstring> param;

      void SetParam(const char *p,int p_len,const xstring &v) {
	 param.add(xstring::get_tmp(p,p_len).c_lc(),new xstring(v.copy()));
      }
      void SetParam(const xstring &p,const xstring &v) {
	 param.add(p,new xstring(v.copy()));
      }
   public:
      Challenge(const char *);
      const xstring& GetScheme() { return scheme; }
      scheme_t GetSchemeCode() { return scheme_code; }
      const xstring& GetRealm() { return GetParam("realm"); }
      const xstring& GetParam(const char *p) {
	 const xstring *v=param.lookup(p);
	 return v?*v:xstring::null;
      }
   };

protected:
   target_t target;
   xstring uri;
   Ref<Challenge> chal;
   xstring user;
   xstring pass;
   HttpHeader header;

   static xstring& append_quoted(xstring& s,const char *n,const char *v);

   // array is enough as there are not too many HttpAuth objects.
   static xarray_p<HttpAuth> cache;

public:
   HttpAuth(target_t t,const char *p_uri,Challenge *p_chal,const char *p_user,const char *p_pass)
      :  target(t), uri(p_uri), chal(p_chal), user(p_user), pass(p_pass),
	 header(t==WWW?"Authorization":"Proxy-Authorization") {}
   virtual ~HttpAuth() {}
   virtual bool IsValid() const { return true; }
   virtual bool Update(const char *p_method,const char *p_uri,const char *entity_hash=0) { return true; }
   const HttpHeader *GetHeader() { return &header; }
   bool ApplicableForURI(const char *) const;
   bool Matches(target_t t,const char *p_uri,const char *p_user);

   static bool New(target_t t,const char *p_uri,
      Challenge *p_chal,const char *p_user,const char *p_pass);
   static HttpAuth *Get(target_t t,const char *p_uri,const char *p_user);
   static void CleanCache(target_t t,const char *p_uri,const char *p_user);
};

class HttpAuthBasic : public HttpAuth
{
   void MakeHeader();
public:
   HttpAuthBasic(target_t t,const char *p_uri,Challenge *p_chal,const char *p_user,const char *p_pass)
      :  HttpAuth(t,p_uri,p_chal,p_user,p_pass) { MakeHeader(); }
};

class HttpAuthDigest : public HttpAuth
{
   xstring cnonce;   // random client nonce
   xstring HA1;	     // "session key" A1 in lower-case hex
   unsigned nc;
   void MakeHA1();
public:
   HttpAuthDigest(target_t t,const char *p_uri,Challenge *p_chal,const char *p_user,const char *p_pass)
      :  HttpAuth(t,p_uri,p_chal,p_user,p_pass), nc(0) { MakeHA1(); }
   bool IsValid() const { return HA1.length()>0; }
   bool Update(const char *p_method,const char *p_uri,const char *entity_hash);
};

#endif//HTTPAUTH_H
