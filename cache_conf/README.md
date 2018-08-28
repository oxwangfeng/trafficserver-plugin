compile:

  /root/trafficserver-6.2.0/tools/tsxs -I /root/trafficserver-6.2.0/lib -I /root/trafficserver-6.2.0/proxy/api/ -L. -o cache_conf.so read_conf.cc cache_conf.cc domain_check.cc  header_modify.cc

format:
	plugin.config:
		cache_conf.so /usr/local/ats/etc/trafficserver/plugin/cache_conf.config (absolutely path)

	cache_conf.config:
		dest_domain=localhost suffix=img ttl-in-cache=30s prefix=/scw
		dest_domain=www.163.com scheme=http action=never-cache
		dest_domain=www.163.com suffix=.html scheme=http revalidate=3600s
		dest_domain=.0.0.1 suffix=.cc scheme=http revalidate=3600

NOTE:
	Only tokens above would be supported this moment.Do not use cache.config and cache_conf plugin at the same
	time.

													scw 
		
