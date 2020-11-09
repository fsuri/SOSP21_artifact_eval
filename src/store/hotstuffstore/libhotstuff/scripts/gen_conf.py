import os, re
import subprocess
import itertools
import argparse

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Generate configuration file for a batch of replicas')
    parser.add_argument('--prefix', type=str, default='hotstuff.gen')
    parser.add_argument('--ips', type=str, default=None)
    parser.add_argument('--iter', type=int, default=1)
    parser.add_argument('--pport', type=int, default=30000)
    parser.add_argument('--cport', type=int, default=40000)
    parser.add_argument('--keygen', type=str, default='./hotstuff-keygen')
    parser.add_argument('--tls-keygen', type=str, default='./hotstuff-tls-keygen')
    parser.add_argument('--nodes', type=str, default='nodes.txt')
    parser.add_argument('--block-size', type=int, default=1)
    parser.add_argument('--stable-period', type=int, default=40)
    parser.add_argument('--pace-maker', type=str, default='rr')
    args = parser.parse_args()

    if args.ips is None:
        # local
        #ips = ['127.0.0.1']
        # datacenter small
        #ips = ['10.0.0.5', '10.0.0.6', '10.0.0.7', '10.0.0.8']
        # datacenter large
        #ips = ['10.0.0.5', '10.0.0.6', '10.0.0.7', '10.0.0.8', '10.0.0.9', '10.0.0.10', '10.0.0.11', '10.0.0.12', '10.0.0.13', '10.0.0.14', '10.0.0.15', '10.0.0.16', '10.0.0.17', '10.0.0.18', '10.0.0.19', '10.0.0.20']
        # geo-distributed
        # ips = ['137.135.57.40',
        #        '137.135.57.40',
        #        '104.45.193.15',
        #        '52.136.116.111']
        # azure 4 local
        # ips = ['10.0.2.6',
        #        '10.0.2.7',
        #        '10.0.2.8',
        #        '10.0.2.9']
        # azure 10 local
        # ips = ['10.0.2.6',
        #        '10.0.2.7',
        #        '10.0.2.8',
        #        '10.0.2.9',
        #        '10.0.2.10',
        #        '10.0.2.11',
        #        '10.0.2.12',
        #        '10.0.2.13',
        #        '10.0.2.14',
        #        '10.0.2.15']
        # azure 16 local
        # ips = ['10.0.2.6',
        #        '10.0.2.7',
        #        '10.0.2.8',
        #        '10.0.2.9',
        #        '10.0.2.10',
        #        '10.0.2.11',
        #        '10.0.2.12',
        #        '10.0.2.13',
        #        '10.0.2.14',
        #        '10.0.2.15',
        #        '10.0.2.16',
        #        '10.0.2.17',
        #        '10.0.2.18',
        #        '10.0.2.19',
        #        '10.0.2.20',
        #        '10.0.2.21']
        # azure 31 local
        # ips = ['10.0.2.6',
        #        '10.0.2.7',
        #        '10.0.2.8',
        #        '10.0.2.9',
        #        '10.0.2.10',
        #        '10.0.2.11',
        #        '10.0.2.12',
        #        '10.0.2.13',
        #        '10.0.2.14',
        #        '10.0.2.15',
        #        '10.0.2.16',
        #        '10.0.2.17',
        #        '10.0.2.18',
        #        '10.0.2.19',
        #        '10.0.2.20',
        #        '10.0.2.21',
        #        '10.0.2.22',
        #        '10.0.2.23',
        #        '10.0.2.24',
        #        '10.0.2.25',
        #        '10.0.2.26',
        #        '10.0.2.27',
        #        '10.0.2.28',
        #        '10.0.2.29',
        #        '10.0.2.30',
        #        '10.0.2.31',
        #        '10.0.2.32',
        #        '10.0.2.33',
        #        '10.0.2.34',
        #        '10.0.2.35',
        #        '10.0.2.36']        
        # azure 4 geo-distributed
        # ips = ['10.0.2.6',
        #        '10.0.3.6',
        #        '10.0.4.6',
        #        '10.0.2.7']
        # azure 10 geo-distributed
        # ips = ['10.0.2.6',
        #        '10.0.3.6',
        #        '10.0.4.6',
        #        '10.0.2.7',
        #        '10.0.3.7',
        #        '10.0.4.7',
        #        '10.0.2.8',
        #        '10.0.3.8',
        #        '10.0.4.8',
        #        '10.0.2.9']
        # azure 16 geo-distributed
        # ips = ['10.0.2.6',
        #        '10.0.3.6',
        #        '10.0.4.6',
        #        '10.0.2.7',
        #        '10.0.3.7',
        #        '10.0.4.7',
        #        '10.0.2.8',
        #        '10.0.3.8',
        #        '10.0.4.8',
        #        '10.0.2.9',
        #        '10.0.3.9',
        #        '10.0.4.9',
        #        '10.0.2.10',
        #        '10.0.3.10',
        #        '10.0.4.10',
        #        '10.0.2.11']
        # azure 31 geo-distributed
        # ips = ['10.0.2.6',
        #        '10.0.3.6',
        #        '10.0.4.6',
        #        '10.0.2.7',
        #        '10.0.3.7',
        #        '10.0.4.7',
        #        '10.0.2.8',
        #        '10.0.3.8',
        #        '10.0.4.8',
        #        '10.0.2.9',
        #        '10.0.3.9',
        #        '10.0.4.9',
        #        '10.0.2.10',
        #        '10.0.3.10',
        #        '10.0.4.10',
        #        '10.0.2.11',
        #        '10.0.3.11',
        #        '10.0.4.11',
        #        '10.0.2.12',
        #        '10.0.3.12',
        #        '10.0.4.12',
        #        '10.0.2.13',
        #        '10.0.3.13',
        #        '10.0.4.13',
        #        '10.0.2.14',
        #        '10.0.3.14',
        #        '10.0.4.14',
        #        '10.0.2.15',
        #        '10.0.3.15',
        #        '10.0.4.15',
        #        '10.0.2.16']
        # azure 61 geo-distributed
        # ips = ['10.0.2.6',
        #        '10.0.3.6',
        #        '10.0.4.6',
        #        '10.0.2.7',
        #        '10.0.3.7',
        #        '10.0.4.7',
        #        '10.0.2.8',
        #        '10.0.3.8',
        #        '10.0.4.8',
        #        '10.0.2.9',
        #        '10.0.3.9',
        #        '10.0.4.9',
        #        '10.0.2.10',
        #        '10.0.3.10',
        #        '10.0.4.10',
        #        '10.0.2.11',
        #        '10.0.3.11',
        #        '10.0.4.11',
        #        '10.0.2.12',
        #        '10.0.3.12',
        #        '10.0.4.12',
        #        '10.0.2.13',
        #        '10.0.3.13',
        #        '10.0.4.13',
        #        '10.0.2.14',
        #        '10.0.3.14',
        #        '10.0.4.14',
        #        '10.0.2.15',
        #        '10.0.3.15',
        #        '10.0.4.15',
        #        '10.0.2.16',
        #        '10.0.3.16',
        #        '10.0.4.16',
        #        '10.0.2.17',
        #        '10.0.3.17',
        #        '10.0.4.17',
        #        '10.0.2.18',
        #        '10.0.3.18',
        #        '10.0.4.18',
        #        '10.0.2.19',
        #        '10.0.3.19',
        #        '10.0.4.19',
        #        '10.0.2.20',
        #        '10.0.3.20',
        #        '10.0.4.20',
        #        '10.0.2.21',
        #        '10.0.3.21',
        #        '10.0.4.21',
        #        '10.0.2.22',
        #        '10.0.3.22',
        #        '10.0.4.22',
        #        '10.0.2.23',
        #        '10.0.3.23',
        #        '10.0.4.23',
        #        '10.0.2.24',
        #        '10.0.3.24',
        #        '10.0.4.24',
        #        '10.0.2.25',
        #        '10.0.3.25',
        #        '10.0.4.25',
        #        '10.0.2.26']

        # azure 100 geo-distributed
        ips = ['10.0.2.6',
               '10.0.3.6',
               '10.0.4.6',
               '10.0.2.7',
               '10.0.3.7',
               '10.0.4.7',
               '10.0.2.8',
               '10.0.3.8',
               '10.0.4.8',
               '10.0.2.9',
               '10.0.3.9',
               '10.0.4.9',
               '10.0.2.10',
               '10.0.3.10',
               '10.0.4.10',
               '10.0.2.11',
               '10.0.3.11',
               '10.0.4.11',
               '10.0.2.12',
               '10.0.3.12',
               '10.0.4.12',
               '10.0.2.13',
               '10.0.3.13',
               '10.0.4.13',
               '10.0.2.14',
               '10.0.3.14',
               '10.0.4.14',
               '10.0.2.15',
               '10.0.3.15',
               '10.0.4.15',
               '10.0.2.16',
               '10.0.3.16',
               '10.0.4.16',
               '10.0.2.17',
               '10.0.3.17',
               '10.0.4.17',
               '10.0.2.18',
               '10.0.3.18',
               '10.0.4.18',
               '10.0.2.19',
               '10.0.3.19',
               '10.0.4.19',
               '10.0.2.20',
               '10.0.3.20',
               '10.0.4.20',
               '10.0.2.21',
               '10.0.3.21',
               '10.0.4.21',
               '10.0.2.22',
               '10.0.3.22',
               '10.0.4.22',
               '10.0.2.23',
               '10.0.3.23',
               '10.0.4.23',
               '10.0.2.24',
               '10.0.3.24',
               '10.0.4.24',
               '10.0.2.25',
               '10.0.3.25',
               '10.0.4.25',
               '10.0.2.26',
               '10.0.3.26',
               '10.0.4.26',
               '10.0.2.27',
               '10.0.3.27',
               '10.0.4.27',
               '10.0.2.28',
               '10.0.3.28',
               '10.0.4.28',
               '10.0.2.29',
               '10.0.3.29',
               '10.0.4.29',
               '10.0.2.30',
               '10.0.3.30',
               '10.0.4.30',
               '10.0.2.31',
               '10.0.3.31',
               '10.0.4.31',
               '10.0.2.32',
               '10.0.3.32',
               '10.0.4.32',
               '10.0.2.33',
               '10.0.3.33',
               '10.0.4.33',
               '10.0.2.34',
               '10.0.3.34',
               '10.0.4.34',
               '10.0.2.35',
               '10.0.3.35',
               '10.0.4.35',
               '10.0.2.36',
               '10.0.3.36',
               '10.0.4.36',
               '10.0.2.37',
               '10.0.3.37',
               '10.0.4.37',
               '10.0.2.38',
               '10.0.3.38',
               '10.0.4.38',
               '10.0.2.39']
    else:
        ips = [l.strip() for l in open(args.ips, 'r').readlines()]

        
    prefix = args.prefix
    iter = args.iter
    base_pport = args.pport
    base_cport = args.cport
    keygen_bin = args.keygen
    tls_keygen_bin = args.tls_keygen

    main_conf = open("./conf-gen/{}.conf".format(prefix), 'w')
    nodes = open('./conf-gen/'+args.nodes, 'w')
    replicas = ["{}:{};{}".format(ip, base_pport + i, base_cport + i)
                for ip in ips
                for i in range(iter)]
    p = subprocess.Popen([keygen_bin, '--num', str(len(replicas))],
                        stdout=subprocess.PIPE, stderr=open(os.devnull, 'w'))
    keys = [[t[4:] for t in l.decode('ascii').split()] for l in p.stdout]
    tls_p = subprocess.Popen([tls_keygen_bin, '--num', str(len(replicas))],
                        stdout=subprocess.PIPE, stderr=open(os.devnull, 'w'))
    tls_keys = [[t[4:] for t in l.decode('ascii').split()] for l in tls_p.stdout]
    if not (args.block_size is None):
        main_conf.write("block-size = {}\n".format(args.block_size))
    if not (args.stable_period is None):
        main_conf.write("stable-period = {}\n".format(args.stable_period))        
    if not (args.pace_maker is None):
        main_conf.write("pace-maker = {}\n".format(args.pace_maker))
    for r in zip(replicas, keys, tls_keys, itertools.count(0)):
        main_conf.write("replica = {}, {}, {}\n".format(r[0], r[1][0], r[2][2]))
        r_conf_name = "./conf-gen/{}-sec{}.conf".format(prefix, r[3])
        nodes.write("{}:{}\t{}\n".format(r[3], r[0], r_conf_name))
        r_conf = open(r_conf_name, 'w')
        r_conf.write("privkey = {}\n".format(r[1][1]))
        r_conf.write("tls-privkey = {}\n".format(r[2][1]))
        r_conf.write("tls-cert = {}\n".format(r[2][0]))
        r_conf.write("idx = {}\n".format(r[3]))
