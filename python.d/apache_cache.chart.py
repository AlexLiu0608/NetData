# -*- coding: utf-8 -*-
# Description: apache cache netdata python.d plugin
# Author: Pawel Krupa (paulfantom)

from base import LogService

priority = 60000
retries = 5
update_every = 3

ORDER = ['cache']
CHARTS = {
    'cache': {
        'options': [None, 'apache cached responses', 'percent cached', 'cached', 'apache_cache.cache', 'stacked'],
        'lines': [
            ["hit", 'cache', "percentage-of-absolute-row"],
            ["miss", None, "percentage-of-absolute-row"]
        ]}
}


class Service(LogService):
    def __init__(self, configuration=None, name=None):
        LogService.__init__(self, configuration=configuration, name=name)
        if len(self.log_path) == 0:
            self.log_path = "/var/log/apache2/cache.log"
        self.order = ORDER
        self.definitions = CHARTS

    def _formatted_data(self):
        """
        Parse new log lines
        :return: dict
        """
        try:
            raw = self._get_data()
            if raw is None:
                return None
        except (ValueError, AttributeError):
            return None

        hit = 0
        miss = 0
        for line in raw:
            if "cache hit" in line:
                hit += 1
            elif "cache miss" in line:
                miss += 1

        total = hit + miss
        if total == 0:
            return None

        return {'hit': int(hit/float(total) * 100),
                'miss': int(miss/float(total) * 100)}
