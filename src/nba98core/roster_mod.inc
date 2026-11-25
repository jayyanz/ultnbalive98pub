/*
 * roster_mod.inc - our roster data merged into a copy of the database (part of nba98core.dll).
 *
 * Roster data files shipped next to nba98core.dll, each built by a script in
 * src/nba98db and holding only our own data (no EA records):
 *   nba98_cutplayers.dat  the 54 cut players as free agents   ([nba98] CutPlayers=1)
 *   nba98_jordan.dat      Michael Jordan in the Roster Player slot ([nba98] Jordan=1)
 * On first launch we build a working database for the game from the user's own files:
 *
 *   <install>\nba98data\dbdata   copy of dbdata   + our PLAYERS and CAREER records
 *   <install>\nba98data\origdb   copy of origdb   + the same (so "restore original rosters" keeps them)
 *   <install>\nba98data\saves    copy of saves    + PLAYERS records in saves\default
 *
 * and point the game's "dbdata", "origdb" and "saves" folders at them through the
 * generated INSTALL.DAT. Original files are never written. The copy is built once
 * (marker nba98data\roster.txt, one line per data file applied, with its CRC) and from
 * then on is the game's live database, so seasons and edits persist. A data file that
 * is new or changed since the marker was written is applied to the existing copy at the
 * next start (every operation is idempotent). Delete the nba98data folder to rebuild.
 *
 * Index files of a table we change (TABLE1.CDX, TABLE2.CDX, TABLE.CDX) are removed: the
 * game builds missing indexes itself (the CD ships none).
 *
 * File format NBA98ADD v1/v2: magic, u32 version, u32 section count; per section a
 * 32-byte header (table name, CRC-32 of the DBF field descriptors it was built for,
 * record length, key offset and length, operation (v2), record count), for MODIFY a
 * record-length byte mask, then the records. Operations:
 *   APPEND   add the records; skipped if all their keys are already there, refused if some are
 *   MODIFY   for each record, copy the masked bytes into the table's records with the same key
 *   REPLACE  remove the table's records with any of our keys, then add ours
 *
 * Settings ([nba98] in nba98.ini): CutPlayers=1, Jordan=1 (defaults). With both 0 the
 * game uses its own folders as before.
 * Included from nba98core.cpp.
 */

static char g_roster_root[MAX_PATH];     /* "<install>\nba98data" without trailing backslash */
static int g_roster_active;

/* ---------------------------------------------------------------- crc32 (zlib polynomial) */
static uint32_t crc32_buf(const unsigned char *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

/* ---------------------------------------------------------------- file helpers */
static unsigned char *rm_read_all(const char *path, DWORD *size)
{
    HANDLE f = o_CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    unsigned char *buf;
    DWORD rd = 0;
    if (f == INVALID_HANDLE_VALUE) return NULL;
    *size = GetFileSize(f, NULL);
    buf = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, *size + 1);
    if (buf && (!ReadFile(f, buf, *size, &rd, NULL) || rd != *size)) { HeapFree(GetProcessHeap(), 0, buf); buf = NULL; }
    CloseHandle(f);
    return buf;
}

static int rm_write_all(const char *path, const void *a, DWORD na, const void *b, DWORD nb, const void *c, DWORD nc)
{
    char tmp[MAX_PATH];
    HANDLE f;
    DWORD w;
    int ok;
    _snprintf(tmp, sizeof tmp, "%s.new", path);
    f = o_CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(f, a, na, &w, NULL) && w == na;
    if (ok && nb) ok = WriteFile(f, b, nb, &w, NULL) && w == nb;
    if (ok && nc) ok = WriteFile(f, c, nc, &w, NULL) && w == nc;
    CloseHandle(f);
    if (!ok || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) { DeleteFileA(tmp); return 0; }
    return 1;
}

static int rm_is_skipped_index(const char *name)
{
    return !_stricmp(name, "PLAYERS1.CDX") || !_stricmp(name, "PLAYERS2.CDX") ||
           !_stricmp(name, "CAREER1.CDX") || !_stricmp(name, "CAREER2.CDX");
}

/* copy a folder (recursively), skipping the indexes of the tables we change */
static int rm_copy_tree(const char *src, const char *dst, int *nfiles)
{
    char pat[MAX_PATH], s[MAX_PATH], d[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int ok = 1;
    if (!CreateDirectoryA(dst, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    _snprintf(pat, sizeof pat, "%s\\*", src);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 1;    /* empty or missing source: nothing to copy */
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        _snprintf(s, sizeof s, "%s\\%s", src, fd.cFileName);
        _snprintf(d, sizeof d, "%s\\%s", dst, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ok = rm_copy_tree(s, d, nfiles) && ok;
        } else if (!rm_is_skipped_index(fd.cFileName)) {
            if (CopyFileA(s, d, FALSE)) {
                SetFileAttributesA(d, FILE_ATTRIBUTE_NORMAL);   /* CodeBase needs read/write */
                (*nfiles)++;
            } else {
                LOG("roster: could not copy %s (error %lu)", s, GetLastError());
                ok = 0;
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return ok;
}

/* ---------------------------------------------------------------- mod files */
enum { RM_APPEND = 0, RM_MODIFY = 1, RM_REPLACE = 2 };

typedef struct {
    char table[17];
    uint32_t desc_crc;
    uint16_t reclen, key_off;
    uint8_t key_len, op;
    uint32_t count;
    const unsigned char *mask;      /* RM_MODIFY only */
    const unsigned char *recs;
    int file;                       /* index into g_rm_files */
} rm_section;

typedef struct {
    const char *name, *ini_key;
    unsigned char *data;
    uint32_t crc;
    int loaded, pending;            /* pending: not yet applied to the existing nba98data */
} rm_file;

static rm_file g_rm_files[] = {
    {"nba98_cutplayers.dat", "CutPlayers"},
    {"nba98_jordan.dat", "Jordan"},
};
#define RM_NFILES ((int)(sizeof g_rm_files / sizeof g_rm_files[0]))
#define RM_MAXSEC 16
static rm_section g_rm_sec[RM_MAXSEC];
static int g_rm_nsec;

static int rm_load(int fi, const char *path)
{
    DWORD size = 0, p;
    uint32_t ver, n, i;
    unsigned char *d = rm_read_all(path, &size);
    int first = g_rm_nsec;
    if (!d) return 0;
    if (size < 16 || memcmp(d, "NBA98ADD", 8)) { LOG("roster: %s is not a roster data file", path); goto bad; }
    memcpy(&ver, d + 8, 4);
    memcpy(&n, d + 12, 4);
    if ((ver != 1 && ver != 2) || n > (uint32_t)(RM_MAXSEC - g_rm_nsec)) {
        LOG("roster: %s has unsupported version %lu or too many sections", path, (unsigned long)ver);
        goto bad;
    }
    p = 16;
    for (i = 0; i < n; i++) {
        rm_section *s = &g_rm_sec[g_rm_nsec];
        if (p + 32 > size) goto bad;
        memset(s, 0, sizeof *s);
        memcpy(s->table, d + p, 16); s->table[16] = 0;
        if (strlen(s->table) > 4 && !_stricmp(s->table + strlen(s->table) - 4, ".DBF"))
            s->table[strlen(s->table) - 4] = 0;            /* v1 files name the table "PLAYERS.DBF" */
        memcpy(&s->desc_crc, d + p + 16, 4);
        memcpy(&s->reclen, d + p + 20, 2);
        memcpy(&s->key_off, d + p + 22, 2);
        s->key_len = d[p + 24];
        s->op = ver >= 2 ? d[p + 25] : RM_APPEND;
        memcpy(&s->count, d + p + 28, 4);
        s->file = fi;
        p += 32;
        if (!s->reclen || !s->key_len || s->key_off + s->key_len > s->reclen || s->op > RM_REPLACE) goto bad;
        if (s->op == RM_MODIFY) {
            if (s->reclen > size - p) goto bad;
            s->mask = d + p;
            p += s->reclen;
        }
        if ((uint64_t)s->count * s->reclen > size - p) goto bad;
        s->recs = d + p;
        p += s->count * s->reclen;
        g_rm_nsec++;
    }
    g_rm_files[fi].data = d;
    g_rm_files[fi].crc = crc32_buf(d, size);
    g_rm_files[fi].loaded = 1;
    return 1;
bad:
    LOG("roster: %s is damaged; not used", path);
    g_rm_nsec = first;
    HeapFree(GetProcessHeap(), 0, d);
    return 0;
}

/* remove the index files of a table so the game rebuilds them (PLAYERS.DBF -> PLAYERS1.CDX, PLAYERS2.CDX, PLAYERS.CDX) */
static void rm_drop_indexes(const char *dbf)
{
    char base[MAX_PATH], p[MAX_PATH];
    static const char *const suffix[] = {"1.CDX", "2.CDX", ".CDX"};
    size_t n = strlen(dbf);
    int i;
    if (n < 5 || n >= sizeof base) return;
    memcpy(base, dbf, n - 4); base[n - 4] = 0;           /* strip ".DBF" */
    for (i = 0; i < 3; i++) {
        _snprintf(p, sizeof p, "%s%s", base, suffix[i]);
        if (DeleteFileA(p)) LOG("roster: removed %s (the game rebuilds it)", p);
    }
}

static int rm_key_eq(const unsigned char *a, const unsigned char *b, const rm_section *s)
{
    return !memcmp(a + s->key_off, b + s->key_off, s->key_len);
}

/* is this table record's key one of the section's keys? */
static int rm_has_key(const rm_section *s, const unsigned char *r)
{
    uint32_t j;
    for (j = 0; j < s->count; j++) if (rm_key_eq(r, s->recs + j * s->reclen, s)) return 1;
    return 0;
}

/* Apply one section to a dBase III table. Returns 1 on success (including "already
 * applied"), 0 on any mismatch; the table is then left unchanged. */
static int rm_apply(const char *dbf, const rm_section *s)
{
    DWORD size = 0, w = 0;
    unsigned char *d = rm_read_all(dbf, &size), *out = NULL;
    uint32_t nrec, nout = 0, i, j, k, present = 0, mine = 0;
    uint16_t hl, rl;
    int ok = 0, changed = 0;
    const char *what = "";
    if (!d) { LOG("roster: %s missing", dbf); return 0; }
    if (size < 32) goto done;
    memcpy(&nrec, d + 4, 4); memcpy(&hl, d + 8, 2); memcpy(&rl, d + 10, 2);
    if (rl != s->reclen || hl < 33 || (uint64_t)hl + (uint64_t)nrec * rl > size) {
        LOG("roster: %s layout differs (record %u bytes, expected %u); not changed", dbf, rl, s->reclen);
        goto done;
    }
    if (crc32_buf(d + 32, hl - 32) != s->desc_crc) {
        LOG("roster: %s field layout differs from the one the data was built for; not changed", dbf);
        goto done;
    }
    /* output: header, records, end-of-file marker (big enough for every case) */
    out = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, hl + (nrec + s->count) * rl + 1);
    if (!out) goto done;
    memcpy(out, d, hl);
#define REC(i) (d + hl + (i) * rl)
#define OUTREC(i) (out + hl + (i) * rl)

    if (s->op == RM_APPEND) {
        for (j = 0; j < s->count; j++)
            for (i = 0; i < nrec; i++)
                if (rm_key_eq(REC(i), s->recs + j * rl, s)) { present++; break; }
        if (present == s->count) { LOG("roster: %s already has these %lu records", dbf, (unsigned long)s->count); ok = 1; goto done; }
        if (present) {
            LOG("roster: %s already has %lu of our %lu keys (user records?); not changed", dbf,
                (unsigned long)present, (unsigned long)s->count);
            goto done;
        }
        memcpy(OUTREC(0), REC(0), nrec * rl);
        memcpy(OUTREC(nrec), s->recs, s->count * rl);
        nout = nrec + s->count;
        changed = 1; what = "added";
    } else if (s->op == RM_MODIFY) {
        memcpy(OUTREC(0), REC(0), nrec * rl);
        nout = nrec;
        for (j = 0; j < s->count; j++) {
            const unsigned char *m = s->recs + j * rl;
            int found = 0;
            for (i = 0; i < nrec; i++) {
                unsigned char *r = OUTREC(i);
                if (!rm_key_eq(r, m, s)) continue;
                found = 1;
                for (k = 0; k < rl; k++)
                    if (s->mask[k] && r[k] != m[k]) { r[k] = m[k]; changed = 1; }
            }
            if (!found) LOG("roster: %s has no record with key %.*s; skipped", dbf, s->key_len, (const char *)m + s->key_off);
        }
        if (!changed) { LOG("roster: %s already has our changes", dbf); ok = 1; goto done; }
        what = "updated";
    } else {   /* RM_REPLACE */
        for (i = 0; i < nrec; i++)
            if (rm_has_key(s, REC(i))) {
                if (mine < s->count && !memcmp(REC(i), s->recs + mine * rl, rl)) present++;
                mine++;
            }
        if (mine == s->count && present == s->count) { LOG("roster: %s already has these %lu records", dbf, (unsigned long)s->count); ok = 1; goto done; }
        for (i = 0; i < nrec; i++)
            if (!rm_has_key(s, REC(i))) memcpy(OUTREC(nout++), REC(i), rl);
        memcpy(OUTREC(nout), s->recs, s->count * rl);
        nout += s->count;
        changed = 1; what = "replaced";
    }
    memcpy(out + 4, &nout, 4);
    out[hl + nout * rl] = 0x1A;
    w = hl + nout * rl + 1;
    ok = rm_write_all(dbf, out, w, NULL, 0, NULL, 0);
    if (ok) rm_drop_indexes(dbf);
    if (s->op == RM_REPLACE)
        LOG("roster: %s: %s %lu rows with %lu (now %lu records)", dbf, ok ? what : "FAILED to replace",
            (unsigned long)mine, (unsigned long)s->count, (unsigned long)nout);
    else
        LOG("roster: %s: %s %lu records (now %lu)", dbf, ok ? what : "FAILED", (unsigned long)s->count, (unsigned long)nout);
#undef REC
#undef OUTREC
done:
    (void)changed;
    if (out) HeapFree(GetProcessHeap(), 0, out);
    HeapFree(GetProcessHeap(), 0, d);
    return ok;
}

/* apply every section of the selected files to the tables of one folder (dst = nba98data\<name>) */
static int rm_apply_folder(const char *dst, const char *const *tables, int only_pending)
{
    char dbf[MAX_PATH];
    int ok = 1, i;
    for (; *tables; tables++) {
        const char *t = strchr(*tables, '\\') ? strchr(*tables, '\\') + 1 : *tables;
        _snprintf(dbf, sizeof dbf, "%s\\%s", dst, *tables);
        for (i = 0; i < g_rm_nsec; i++) {
            const rm_section *s = &g_rm_sec[i];
            if (_strnicmp(s->table, t, strlen(s->table)) || t[strlen(s->table)] != '.') continue;
            if (only_pending && !g_rm_files[s->file].pending) continue;
            if (GetFileAttributesA(dbf) == INVALID_FILE_ATTRIBUTES) { LOG("roster: %s not present, skipped", dbf); break; }
            ok = rm_apply(dbf, s) && ok;
        }
    }
    return ok;
}

static const char *const k_db_tables[] = {"PLAYERS.DBF", "CAREER.DBF", NULL};
static const char *const k_save_tables[] = {"default\\PLAYERS.DBF", NULL};

static int rm_apply_all(int only_pending)
{
    char dst[MAX_PATH];
    int ok;
    _snprintf(dst, sizeof dst, "%s\\dbdata", g_roster_root);
    ok = rm_apply_folder(dst, k_db_tables, only_pending);
    _snprintf(dst, sizeof dst, "%s\\origdb", g_roster_root);
    ok = rm_apply_folder(dst, k_db_tables, only_pending) && ok;
    _snprintf(dst, sizeof dst, "%s\\saves", g_roster_root);
    ok = rm_apply_folder(dst, k_save_tables, only_pending) && ok;
    return ok;
}

static int rm_copy_folder(const char *name)
{
    char src[MAX_PATH], dst[MAX_PATH];
    int n = 0, ok;
    _snprintf(src, sizeof src, "%s%s", g_exe_dir, name);
    _snprintf(dst, sizeof dst, "%s\\%s", g_roster_root, name);
    ok = rm_copy_tree(src, dst, &n);
    LOG("roster: copied %d files from %s to %s%s", n, src, dst, ok ? "" : " (with errors)");
    return ok;
}

/* marker line for a data file */
static void rm_marker_line(int fi, char *out, size_t n)
{
    _snprintf(out, n, "mod %s crc32 %08lx", g_rm_files[fi].name, (unsigned long)g_rm_files[fi].crc);
    out[n - 1] = 0;
}

/* write roster.txt: a line per loaded file, plus lines of earlier files that are now switched off */
static int rm_write_marker(const char *marker, const char *old)
{
    char buf[2048], line[128];
    size_t len;
    int fi;
    len = (size_t)_snprintf(buf, sizeof buf, "nba98core roster build\r\n");
    for (fi = 0; fi < RM_NFILES; fi++) {
        if (g_rm_files[fi].loaded) rm_marker_line(fi, line, sizeof line);
        else if (old) {
            /* keep the old line for a file that is not loaded now: it is still in the database */
            char key[64];
            const char *p;
            _snprintf(key, sizeof key, "mod %s ", g_rm_files[fi].name);
            if (!(p = strstr(old, key))) continue;
            _snprintf(line, sizeof line, "%.*s", (int)strcspn(p, "\r\n"), p);
        } else continue;
        len += (size_t)_snprintf(buf + len, sizeof buf - len, "%s\r\n", line);
        if (len >= sizeof buf) return 0;
    }
    return rm_write_all(marker, buf, (DWORD)len, NULL, 0, NULL, 0);
}

static void roster_setup(void)
{
    char path[MAX_PATH], marker[MAX_PATH], line[128];
    unsigned char *old;
    DWORD oldsize = 0;
    int fi, nloaded = 0, npending = 0;

    for (fi = 0; fi < RM_NFILES; fi++) {
        if (!ini_int(g_rm_files[fi].ini_key, 1)) { LOG("roster: %s=0, %s not used", g_rm_files[fi].ini_key, g_rm_files[fi].name); continue; }
        _snprintf(path, sizeof path, "%s%s", g_exe_dir, g_rm_files[fi].name);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) { LOG("roster: %s not found, not used", path); continue; }
        if (rm_load(fi, path)) nloaded++;
    }
    if (!nloaded) { LOG("roster: no roster data in use; original database used"); return; }
    _snprintf(g_roster_root, sizeof g_roster_root, "%snba98data", g_exe_dir);
    _snprintf(marker, sizeof marker, "%s\\roster.txt", g_roster_root);

    old = rm_read_all(marker, &oldsize);
    if (old) {
        old[oldsize] = 0;
        g_roster_active = 1;
        for (fi = 0; fi < RM_NFILES; fi++) {
            char key[64];
            if (g_rm_files[fi].loaded) {
                rm_marker_line(fi, line, sizeof line);
                if (!strstr((char *)old, line)) { g_rm_files[fi].pending = 1; npending++; }
            } else {
                _snprintf(key, sizeof key, "mod %s ", g_rm_files[fi].name);
                if (strstr((char *)old, key))
                    LOG("roster: %s is switched off but already in %s; delete the folder to remove it", g_rm_files[fi].name, g_roster_root);
            }
        }
        if (!npending) {
            LOG("roster: using the existing database in %s (delete the folder to rebuild)", g_roster_root);
        } else if (rm_apply_all(1) && rm_write_marker(marker, (char *)old)) {
            LOG("roster: updated the existing database in %s with %d new or changed data file(s)", g_roster_root, npending);
        } else {
            LOG("roster: update of %s incomplete; it is used as it is and the update is retried at the next start", g_roster_root);
        }
        HeapFree(GetProcessHeap(), 0, old);
        return;
    }
    /* 0.7.20: the folder also holds the generated INSTALL.DAT, so it can exist with no
     * database in it; only a dbdata copy without roster.txt counts as an incomplete build */
    _snprintf(path, sizeof path, "%s\\dbdata", g_roster_root);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        LOG("roster: %s exists but is incomplete (no roster.txt); original database used", g_roster_root);
        return;
    }
    if (!CreateDirectoryA(g_roster_root, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        LOG("roster: cannot create %s (error %lu); original database used", g_roster_root, GetLastError());
        return;
    }
    if (rm_copy_folder("dbdata") && rm_copy_folder("origdb") && rm_copy_folder("saves") &&
        rm_apply_all(0) && rm_write_marker(marker, NULL)) {
        g_roster_active = 1;
        LOG("roster: built %s; the game uses it for dbdata, origdb and saves", g_roster_root);
        return;
    }
    LOG("roster: build failed; original database used (fix the cause and delete %s to retry)", g_roster_root);
}

/* INSTALL.DAT folder name -> redirected path (relative to the install root), or NULL */
static const char *roster_redirect(const char *vname)
{
    if (!g_roster_active) return NULL;
    if (!_stricmp(vname, "dbdata")) return "nba98data\\dbdata";
    if (!_stricmp(vname, "origdb")) return "nba98data\\origdb";
    if (!_stricmp(vname, "saves")) return "nba98data\\saves";
    return NULL;
}

/* the saves folder the game is using, relative to the install root (for our own code that opens saves\ files) */
static const char *roster_saves_dir(void)
{
    return g_roster_active ? "nba98data\\saves" : "saves";
}
