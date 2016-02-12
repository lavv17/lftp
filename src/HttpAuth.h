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
#include "buffer.h"

class HttpAuth
{
public:
   enum target_t { WWW, PROXY };

   class Challenge
   {
      xstring scheme;
      xmap_p<xstring> param;

      void SetParam(const char *p,int p_len,const xstring &v) {
	 param.add(xstring::get_tmp(p,p_len).c_lc(),new xstring(v.copy()));
      }
   public:
      Challenge(const char *);
      const xstring& GetScheme() { return scheme; }
      const xstring& GetRealm() { return GetParam("realm"); }
      const xstring& GetParam(const char *p) { return *param.lookup(p); }
   };

protected:
   target_t target;
   xstring uri;
   Ref<Challenge> chal;
   xstring user;
   xstring pass;
   const char *GetHeader() { return target==WWW?"Authorization":"Proxy-Authorization"; }

   // array is enough as there are not too many HttpAuth objects.
   static xarray_p<HttpAuth> cache;

public:
   HttpAuth(target_t t,const char *p_uri,Challenge *p_chal,const char *p_user,const char *p_pass)
      :  target(t), uri(p_uri), chal(p_chal), user(p_user), pass(p_pass) {}
   virtual ~HttpAuth() {}
   virtual void Send(const SMTaskRef<IOBuffer> &buf) = 0;
   bool ApplicableForURI(const char *) const;
   bool Matches(target_t t,const char *p_uri,const char *p_user);

   static bool New(target_t t,const char *p_uri,
      Challenge *p_chal,const char *p_user,const char *p_pass);
   static HttpAuth *Get(target_t t,const char *p_uri,const char *p_user);
   static void CleanCache(target_t t,const char *p_uri,const char *p_user);
};

class HttpAuthBasic : public HttpAuth
{
public:
   HttpAuthBasic(target_t t,const char *p_uri,Challenge *p_chal,const char *p_user,const char *p_pass)
      :  HttpAuth(t,p_uri,p_chal,p_user,p_pass) {}
   void Send(const SMTaskRef<IOBuffer> &buf);
};

class HttpAuthDigest : public HttpAuth
{
public:
   HttpAuthDigest(target_t t,const char *p_uri,Challenge *p_chal,const char *p_user,const char *p_pass)
      :  HttpAuth(t,p_uri,p_chal,p_user,p_pass) {}
};

#endif//HTTPAUTH_H
