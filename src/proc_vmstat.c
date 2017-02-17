#include "common.h"

int do_proc_vmstat(int update_every, usec_t dt) {
    (void)dt;

    static procfile *ff = NULL;
    static int do_swapio = -1, do_io = -1, do_pgfaults = -1, do_numa = -1;
    static int has_numa = -1;

    static ARL_BASE *arl_base = NULL;
    static unsigned long long numa_foreign = 0ULL;
    static unsigned long long numa_hint_faults = 0ULL;
    static unsigned long long numa_hint_faults_local = 0ULL;
    static unsigned long long numa_huge_pte_updates = 0ULL;
    static unsigned long long numa_interleave = 0ULL;
    static unsigned long long numa_local = 0ULL;
    static unsigned long long numa_other = 0ULL;
    static unsigned long long numa_pages_migrated = 0ULL;
    static unsigned long long numa_pte_updates = 0ULL;
    static unsigned long long pgfault = 0ULL;
    static unsigned long long pgmajfault = 0ULL;
    static unsigned long long pgpgin = 0ULL;
    static unsigned long long pgpgout = 0ULL;
    static unsigned long long pswpin = 0ULL;
    static unsigned long long pswpout = 0ULL;

    if(unlikely(!arl_base)) {
        do_swapio = config_get_boolean_ondemand("plugin:proc:/proc/vmstat", "swap i/o", CONFIG_ONDEMAND_ONDEMAND);
        do_io = config_get_boolean("plugin:proc:/proc/vmstat", "disk i/o", 1);
        do_pgfaults = config_get_boolean("plugin:proc:/proc/vmstat", "memory page faults", 1);
        do_numa = config_get_boolean_ondemand("plugin:proc:/proc/vmstat", "system-wide numa metric summary", CONFIG_ONDEMAND_ONDEMAND);


        arl_base = arl_create("vmstat", NULL, 60);
        arl_expect(arl_base, "pgfault", &pgfault);
        arl_expect(arl_base, "pgmajfault", &pgmajfault);
        arl_expect(arl_base, "pgpgin", &pgpgin);
        arl_expect(arl_base, "pgpgout", &pgpgout);
        arl_expect(arl_base, "pswpin", &pswpin);
        arl_expect(arl_base, "pswpout", &pswpout);

        if(do_numa == CONFIG_ONDEMAND_YES || (do_numa == CONFIG_ONDEMAND_ONDEMAND && get_numa_node_count() >= 2)) {
            arl_expect(arl_base, "numa_foreign", &numa_foreign);
            arl_expect(arl_base, "numa_hint_faults_local", &numa_hint_faults_local);
            arl_expect(arl_base, "numa_hint_faults", &numa_hint_faults);
            arl_expect(arl_base, "numa_huge_pte_updates", &numa_huge_pte_updates);
            arl_expect(arl_base, "numa_interleave", &numa_interleave);
            arl_expect(arl_base, "numa_local", &numa_local);
            arl_expect(arl_base, "numa_other", &numa_other);
            arl_expect(arl_base, "numa_pages_migrated", &numa_pages_migrated);
            arl_expect(arl_base, "numa_pte_updates", &numa_pte_updates);
        }
        else {
            // Do not expect numa metrics when they are not needed.
            // By not adding them, the ARL will stop processing the file
            // when all the expected metrics are collected.
            // Also ARL will not parse their values.
            has_numa = 0;
            do_numa = CONFIG_ONDEMAND_NO;
        }
    }

    if(unlikely(!ff)) {
        char filename[FILENAME_MAX + 1];
        snprintfz(filename, FILENAME_MAX, "%s%s", netdata_configured_host_prefix, "/proc/vmstat");
        ff = procfile_open(config_get("plugin:proc:/proc/vmstat", "filename to monitor", filename), " \t:", PROCFILE_FLAG_DEFAULT);
        if(unlikely(!ff)) return 1;
    }

    ff = procfile_readall(ff);
    if(unlikely(!ff)) return 0; // we return 0, so that we will retry to open it next time

    size_t lines = procfile_lines(ff), l;

    arl_begin(arl_base);
    for(l = 0; l < lines ;l++) {
        size_t words = procfile_linewords(ff, l);
        if(unlikely(words < 2)) {
            if(unlikely(words)) error("Cannot read /proc/vmstat line %zu. Expected 2 params, read %zu.", l, words);
            continue;
        }

        if(unlikely(arl_check(arl_base,
                procfile_lineword(ff, l, 0),
                procfile_lineword(ff, l, 1)))) break;
    }

    // --------------------------------------------------------------------

    if(pswpin || pswpout || do_swapio == CONFIG_ONDEMAND_YES) {
        do_swapio = CONFIG_ONDEMAND_YES;

        static RRDSET *st_swapio = NULL;
        if(unlikely(!st_swapio)) {
            st_swapio = rrdset_create("system", "swapio", NULL, "swap", NULL, "Swap I/O", "kilobytes/s", 250, update_every, RRDSET_TYPE_AREA);

            rrddim_add(st_swapio, "in",  NULL, sysconf(_SC_PAGESIZE), 1024, RRDDIM_ALGORITHM_INCREMENTAL);
            rrddim_add(st_swapio, "out", NULL, -sysconf(_SC_PAGESIZE), 1024, RRDDIM_ALGORITHM_INCREMENTAL);
        }
        else rrdset_next(st_swapio);

        rrddim_set(st_swapio, "in", pswpin);
        rrddim_set(st_swapio, "out", pswpout);
        rrdset_done(st_swapio);
    }

    // --------------------------------------------------------------------

    if(do_io) {
        static RRDSET *st_io = NULL;
        if(unlikely(!st_io)) {
            st_io = rrdset_create("system", "io", NULL, "disk", NULL, "Disk I/O", "kilobytes/s", 150, update_every, RRDSET_TYPE_AREA);

            rrddim_add(st_io, "in",  NULL,  1, 1, RRDDIM_ALGORITHM_INCREMENTAL);
            rrddim_add(st_io, "out", NULL, -1, 1, RRDDIM_ALGORITHM_INCREMENTAL);
        }
        else rrdset_next(st_io);

        rrddim_set(st_io, "in", pgpgin);
        rrddim_set(st_io, "out", pgpgout);
        rrdset_done(st_io);
    }

    // --------------------------------------------------------------------

    if(do_pgfaults) {
        static RRDSET *st_pgfaults = NULL;
        if(unlikely(!st_pgfaults)) {
            st_pgfaults = rrdset_create("mem", "pgfaults", NULL, "system", NULL, "Memory Page Faults", "page faults/s", 500, update_every, RRDSET_TYPE_LINE);
            st_pgfaults->isdetail = 1;

            rrddim_add(st_pgfaults, "minor",  NULL,  1, 1, RRDDIM_ALGORITHM_INCREMENTAL);
            rrddim_add(st_pgfaults, "major", NULL, -1, 1, RRDDIM_ALGORITHM_INCREMENTAL);
        }
        else rrdset_next(st_pgfaults);

        rrddim_set(st_pgfaults, "minor", pgfault);
        rrddim_set(st_pgfaults, "major", pgmajfault);
        rrdset_done(st_pgfaults);
    }

    // --------------------------------------------------------------------

    // Ondemand criteria for NUMA. Since this won't change at run time, we
    // check it only once. We check whether the node count is >= 2 because
    // single-node systems have uninteresting statistics (since all accesses
    // are local).
    if(unlikely(has_numa == -1))
        has_numa = (numa_local || numa_foreign || numa_interleave || numa_other || numa_pte_updates ||
                     numa_huge_pte_updates || numa_hint_faults || numa_hint_faults_local || numa_pages_migrated) ? 1 : 0;

    if(do_numa == CONFIG_ONDEMAND_YES || (do_numa == CONFIG_ONDEMAND_ONDEMAND && has_numa)) {
        do_numa = CONFIG_ONDEMAND_YES;

        static RRDSET *st_numa = NULL;
        if(unlikely(!st_numa)) {
            st_numa = rrdset_create("mem", "numa", NULL, "numa", NULL, "NUMA events", "events/s", 800, update_every, RRDSET_TYPE_LINE);
            st_numa->isdetail = 1;

            // These depend on CONFIG_NUMA in the kernel.
            rrddim_add(st_numa, "local", NULL, 1, 1, RRDDIM_ALGORITHM_INCREMENTAL);
            rrddim_add(st_numa, "foreign", NULL, 1, 1, RRDDIM_ALGORITHM_INCREMENTAL);
            rrddim_add(st_numa, "interleave", NULL, 1, 1, RRDDIM_ALGORITHM_INCREMENTAL);
            rrddim_add(st_numa, "other", NULL, 1, 1, RRDDIM_ALGORITHM_INCREMENTAL);

            // The following stats depend on CONFIG_NUMA_BALANCING in the
            // kernel.
            rrddim_add(st_numa, "pte updates", NULL, 1, 1, RRDDIM_ALGORITHM_INCREMENTAL);
            rrddim_add(st_numa, "huge pte updates", NULL, 1, 1, RRDDIM_ALGORITHM_INCREMENTAL);
            rrddim_add(st_numa, "hint faults", NULL, 1, 1, RRDDIM_ALGORITHM_INCREMENTAL);
            rrddim_add(st_numa, "hint faults local", NULL, 1, 1, RRDDIM_ALGORITHM_INCREMENTAL);
            rrddim_add(st_numa, "pages migrated", NULL, 1, 1, RRDDIM_ALGORITHM_INCREMENTAL);
        }
        else rrdset_next(st_numa);

        rrddim_set(st_numa, "local", numa_local);
        rrddim_set(st_numa, "foreign", numa_foreign);
        rrddim_set(st_numa, "interleave", numa_interleave);
        rrddim_set(st_numa, "other", numa_other);

        rrddim_set(st_numa, "pte updates", numa_pte_updates);
        rrddim_set(st_numa, "huge pte updates", numa_huge_pte_updates);
        rrddim_set(st_numa, "hint faults", numa_hint_faults);
        rrddim_set(st_numa, "hint faults local", numa_hint_faults_local);
        rrddim_set(st_numa, "pages migrated", numa_pages_migrated);

        rrdset_done(st_numa);
    }

    return 0;
}

