tsxs -ltscommon  -I ~/trafficserver/plugins/ts-module/ts-cm/ -o libwildcommon.so wildcard_common.cc || exit 1
cp libwildcommon.so /usr/lib/
tsxs -lwildcommon -I ~/trafficserver/plugins/ts-module/ts-cm/ -o wild_purge.so wildcard_purge.cc || exit 1
tsxs -lwildcommon -I ~/trafficserver/plugins/ts-module/ts-cm/ -o wild_global.so wildcard_global.cc || exit 1
cp wild_purge.so /opt/fusion/cdn/trafficserver/libexec/trafficserver/
cp wild_global.so /opt/fusion/cdn/trafficserver/libexec/trafficserver/
