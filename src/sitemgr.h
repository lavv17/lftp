/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2015 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef NBOOKMARK_H
#define NBOOKMARK_H

#include <sys/types.h>
#include "keyvalue.h"

#include <json-c/json.h>

class Sitemgr {
    xstring sites_file;
    xstring last_used_remote;
    int sites_fd;
    time_t stamp;


    void Load();

    void PreModify();
    void PostModify();
    void Clear();



    json_object *SiteList2JsonObj();
    const char *SiteList2JsonString();

    const char *Site2JsonString(const char *sitename);
    json_object *Site2JsonObj(const char *sitename);


public:

    typedef struct Site {
        char *sitename;
        //
        char *address;
        char *port;
        bool ssl;
        //
        char *username;
        char *password;
        //
        char *notes;
        char *localPath;
        char *remotePath;

        Site *next, *prev;
    } Site;

    typedef struct SiteList {
        Site *head, *tail;
        int size;
    } SiteList;

    void Add(const char *sitename);
    void Remove(const char *sitename);
    Site *GetSite(const char *sitename);
    void Refresh();

    char *Conf(const char* sitename);
    void ConfSet(const char* sitename, const char* field, const char *value);

    char *Format();
    char *FormatHidePasswords();
    int SiteCount();

    char *List();
    char *ListParsable(const char* delimiter);
    Site *SiteAt(int position);
    void Save();
    bool SiteExists(const char *sitename);
    const char* GetLastUsedRemote() const { return last_used_remote; };
    void SetLastUsedRemote(const char *sitename) { last_used_remote.set(sitename); };

    Sitemgr();
    ~Sitemgr();

    void Rewind() {
    }

    const xstring& GetFilePath() const {
        return sites_file;
    }

private:
    void InitializeSite(Site *site);
    Site *JsonObj2Site(json_object *json_site);
    void AppendSite(Site *site);
    void FreeSite(Site *site);


};

extern Sitemgr lftp_sitemgr;

#endif //NBOOKMARK_H
