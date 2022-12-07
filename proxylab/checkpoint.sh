# port=8092
# make || exit 1
# ./proxy ${port} &
# pxy/pxydrive.py -P localhost:${port} -f tests/A05-large-text.cmd
# kill %1
make && ./driver.sh check
