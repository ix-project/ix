/*
 * Copyright 2013-16 Board of Trustees of Stanford University
 * Copyright 2013-16 Ecole Polytechnique Federale Lausanne (EPFL)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * cfg.c - configuration parameters
 *
 * parsing of the configuration file parameters. All existing parameters are
 * defined in struct config_tbl, along with their respective handlers.
 * In order to add a parameter, add a corresponding entry to config_tbl along
 * with an appropriate handler which will store the setting into the global
 * settings data struct.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <libconfig.h>	/* provides hierarchical config file parsing */

#include <ix/errno.h>
#include <ix/log.h>
#include <ix/types.h>
#include <ix/cfg.h>
#include <ix/cpu.h>

#include <net/ethernet.h>
#include <net/ip.h>
#include <ix/ethdev.h>

#define DEFAULT_CONF_FILE "./ix.conf"

struct cfg_parameters CFG;

extern int net_cfg(void);
extern int arp_insert(struct ip_addr *addr, struct eth_addr *mac);

static config_t cfg;
static char config_file[256];

static int parse_host_addr(void);
static int parse_port(void);
static int parse_gateway_addr(void);
static int parse_arp(void);
static int parse_devices(void);
static int parse_cpu(void);
static int parse_batch(void);
static int parse_loader_path(void);

struct config_vector_t {
	const char *name;
	int (*f)(void);
};

static struct config_vector_t config_tbl[] = {
	{ "host_addr",    parse_host_addr},
	{ "port",         parse_port},
	{ "gateway_addr", parse_gateway_addr},
	{ "arp",          parse_arp},
	{ "devices",      parse_devices},
	{ "cpu",          parse_cpu},
	{ "batch",        parse_batch},
	{ "loader_path",  parse_loader_path},
	{ NULL,           NULL}
};

/**
 * IMPORTANT NOTE about the following parsers: libconfig allocates memory on
 * any 'xx_lookup' calls. According to the docs this memory is managed by the
 * lib and freed "when the setting is destroyed or when the setting’s value
 * is changed; the string must not be freed by the caller."
 * FIXME: ensure the above is true.
*/

static int str_to_eth_addr(const char *src, unsigned char *dst)
{
	struct eth_addr tmp;

	if (sscanf(src, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
		   &tmp.addr[0], &tmp.addr[1], &tmp.addr[2],
		   &tmp.addr[3], &tmp.addr[4], &tmp.addr[5]) != 6)
		return -EINVAL;
	memcpy(dst, &tmp, sizeof(tmp));
	return 0;
}

/**
 * str_to_ip - converts char ip in presentation format to binary format
 * @src: the ip address in presentation format
 * @dst: the buffer to store the 32bit result
 * */
static int str_to_ip_addr(const char *src, unsigned char *dst)
{
	uint32_t addr;
	unsigned char a, b, c, d;
	if (sscanf(src, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4) {
		return -EINVAL;
	}
	addr = MAKE_IP_ADDR(a, b, c, d);
	memcpy(dst, &addr, sizeof(addr));
	return 0;
}

static int parse_arp(void)
{
	const config_setting_t *arp = NULL, *entry = NULL;
	int i, ret;

	arp = config_lookup(&cfg, "arp");
	if (!arp) {
		log_info("no static arp entries defined in config");
		return 0;
	}
	for (i = 0; i < config_setting_length(arp); ++i) {
		const char *ip = NULL, *mac = NULL;
		struct ip_addr ipaddr;
		struct eth_addr macaddr;
		entry = config_setting_get_elem(arp, i);
		config_setting_lookup_string(entry, "ip", &ip);
		config_setting_lookup_string(entry, "mac", &mac);
		if (!ip || !mac)
			return -EINVAL;
		if (str_to_ip_addr(ip, (void *)&ipaddr))
			return -EINVAL;
		if (str_to_eth_addr(mac, (void *)&macaddr))
			return -EINVAL;
		ret = arp_insert(&ipaddr, &macaddr);
		if (ret) {
			log_err("cfg: failed to insert static ARP entry.\n");
			return ret;
		}
	}
	return 0;
}

static int parse_gateway_addr(void)
{
	char *parsed = NULL;

	config_lookup_string(&cfg, "gateway_addr", (const char **)&parsed);
	if (!parsed)
		return -EINVAL;
	if (str_to_ip_addr(parsed, (void *)&CFG.gateway_addr))
		return -EINVAL;
	return 0;
}

static int add_port(int port)
{
	if (port <= 0 || port > 65534)
		return -EINVAL;
	CFG.ports[CFG.num_ports] = (uint16_t)port;
	++CFG.num_ports;
	return 0;
}

static int parse_port(void)
{
	const config_setting_t *ports = NULL;
	int port, ret;

	ports = config_lookup(&cfg, "port");
	if (!ports)
		return -EINVAL;
	port = config_setting_get_int(ports);
	if (port)
		return add_port(port);
	CFG.num_ports = 0;
	while (CFG.num_ports < CFG_MAX_PORTS && CFG.num_ports < config_setting_length(ports)) {
		port = 0;
		port = config_setting_get_int_elem(ports, CFG.num_ports);
		ret = add_port(port);
		if (ret)
			return ret;
	}
	return 0;
}

static int parse_host_addr(void)
{
	char *parsed = NULL, *ip = NULL, *bitmask = NULL;

	config_lookup_string(&cfg, "host_addr", (const char **)&parsed);
	if (!parsed)
		return -EINVAL;
	/* IP */
	ip = strtok(parsed, "/");
	if (!ip)
		return -EINVAL;
	if (str_to_ip_addr(ip, (void *)&CFG.host_addr))
		return -EINVAL;
	/* netmask */
	bitmask = strtok(NULL, "\0");
	if (!bitmask || !atoi(bitmask))
		return -EINVAL;
	CFG.mask = ~(0xFFFFFFFF >> atoi(bitmask));
	/* broadcast */
	CFG.broadcast_addr.addr = CFG.host_addr.addr | ~CFG.mask;
	return 0;
}

static int add_dev(const char *dev)
{
	int ret, i;
	struct pci_addr addr;

	ret = pci_str_to_addr(dev, &addr);
	if (ret) {
		log_err("cfg: invalid device name %s\n", dev);
		return ret;
	}
	for (i = 0; i < CFG.num_ethdev; ++i) {
		if (!memcmp(&CFG.ethdev[i], &addr, sizeof(struct pci_addr)))
			return 0;
	}
	if (CFG.num_ethdev >= CFG_MAX_ETHDEV)
		return -E2BIG;
	CFG.ethdev[CFG.num_ethdev++] = addr;
	return 0;
}

static int parse_devices(void)
{
	const config_setting_t *devs = NULL;
	const char *dev = NULL;
	int i, ret;

	devs = config_lookup(&cfg, "devices");
	if (!devs)
		return -EINVAL;
	dev = config_setting_get_string(devs);
	if (dev)
		return add_dev(dev);
	for (i = 0; i < config_setting_length(devs); ++i) {
		dev = NULL;
		dev = config_setting_get_string_elem(devs, i);
		ret = add_dev(dev);
		if (ret)
			return ret;
	}
	return 0;
}

static int add_cpu(int cpu)
{
	int i;

	if (cpu < 0 || cpu >= cpu_count) {
		log_err("cfg: cpu %d is invalid (min:0 max:%d)\n",
			cpu, cpu_count);
		return -EINVAL;
	}
	for (i = 0; i < CFG.num_cpus; i++) {
		if (CFG.cpu[i] == cpu)
			return 0;
	}
	if (CFG.num_cpus >= CFG_MAX_CPU)
		return -E2BIG;
	CFG.cpu[CFG.num_cpus++] = (uint32_t)cpu;
	return 0;
}

static int parse_cpu(void)
{
	int i, ret, cpu = -1;
	config_setting_t *cpus = NULL;

	cpus = config_lookup(&cfg, "cpu");
	if (!cpus) {
		return -EINVAL;
	}
	if (!config_setting_get_elem(cpus, 0)) {
		cpu = config_setting_get_int(cpus);
		return add_cpu(cpu);
	}
	for (i = 0; i < config_setting_length(cpus); ++i) {
		cpu = config_setting_get_int_elem(cpus, i);
		ret = add_cpu(cpu);
		if (ret) {
			return ret;
		}
	}
	return 0;
}

static int parse_batch(void)
{
	int batch = -1;
	config_lookup_int(&cfg, "batch", &batch);
	if (!batch || batch <= 0) {
		return -EINVAL;
	}
	eth_rx_max_batch = batch;
	return 0;
}

static int parse_loader_path(void)
{
	char *parsed = NULL;

	config_lookup_string(&cfg, "loader_path", (const char **)&parsed);
	if (!parsed)
		return -EINVAL;
	strncpy(CFG.loader_path, parsed, sizeof(CFG.loader_path));
	CFG.loader_path[sizeof(CFG.loader_path) - 1] = '\0';
	return 0;
}

static int parse_conf_file(const char *path)
{
	int ret, i;

	log_info("using config :'%s'\n", path);
	config_init(&cfg);
	if (!config_read_file(&cfg, path)) {
		fprintf(stderr, "%s:%d - %s\n",
			config_error_file(&cfg),
			config_error_line(&cfg),
			config_error_text(&cfg));
		config_destroy(&cfg);
		return -EINVAL;
	}

	for (i = 0; config_tbl[i].name; ++i) {
		if (config_tbl[i].f) {
			ret = config_tbl[i].f();
			if (ret) {
				log_err("error parsing parameter '%s'\n",
					config_tbl[i].name);
				config_destroy(&cfg);
				return ret;
			}
		}
	}
	config_destroy(&cfg);
	return 0;
}

static void usage(char *argv[])
{
	fprintf(stderr, "Usage : %s [option] -- ...\n"
		"\n"
		"Options\n"
		"--config|-c [CONFIG_FILE]\n"
		"\tUse CONFIG_FILE as default config.\n"
		"--log|-l\n"
		"\tSets log level: 0:EMERG, 1:CRIT, 2:ERR, 3:WARN, 4:INFO, 5:DEBUG. Default: 5\n"
		, argv[0]);
}

static int parse_arguments(int argc, char *argv[], int *args_parsed)
{
	int c, ret;
	static struct option long_options[] = {
		{"config", required_argument, NULL, 'c'},
		{"log", required_argument, NULL, 'l'},
		{NULL, 0, NULL, 0}
	};
	static const char *optstring = "c:l:";

	while (true) {
		c = getopt_long(argc, argv, optstring, long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'c':
			strncpy(config_file, optarg, sizeof(config_file));
			config_file[sizeof(config_file) - 1] = '\0';
			break;
		case 'l':
			if (*optarg < 0 || *optarg > 5) {
				fprintf(stderr, "cfg: invalid log parameter");
				ret = -EINVAL;
				goto fail;
			}
			max_loglevel = *optarg;
			break;
		default:
			fprintf(stderr, "cfg: invalid command option %x\n", c);
			ret = -EINVAL;
			goto fail;
		}
	}
	*args_parsed = optind;
	return 0;

fail:
	usage(argv);
	return ret;
}

/**
 * cfg_init - parses configuration arguments and files
 * @argc: the number of arguments
 * @argv: the argument vector
 * @args_parsed: a pointer to store the number of arguments parsed
 *
 * Returns 0 if successful, otherwise fail.
 */
int cfg_init(int argc, char *argv[], int *args_parsed)
{
	int ret;
	sprintf(config_file, DEFAULT_CONF_FILE);

	ret = parse_arguments(argc, argv, args_parsed);
	if (ret)
		return ret;
	ret = parse_conf_file(config_file);
	if (ret)
		return ret;
	ret = net_cfg();
	if (ret)
		return ret;
	return 0;
}

