import os
import subprocess
import sys
import re


def main():
    exe_file = sys.argv[1]
    src = open(sys.argv[2], 'r')
    dst = open(sys.argv[3], 'w')
    addr_to_line_cmd = ['bash', '-c', 'addr2line -e {0} -f'.format(exe_file)]
    p = subprocess.Popen(addr_to_line_cmd,
                         stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT,
                         close_fds=True, preexec_fn=os.setsid)

    addr_line_regex = re.compile('#\d+ (0x[0-9a-fA-F]+) \(')
    src_line = src.readline()
    while src_line:
        addr_match = addr_line_regex.search(src_line)
        if addr_match:
            p.stdin.write('{0}\n'.format(addr_match.groups()[0]))
            out_line1 = p.stdout.readline()
            out_line1.strip('\r\n')
            out_line2 = p.stdout.readline()
            out_line2.strip('\r\n')
            dst.write('{0} {1}:{2}'.format(
                src_line.strip('\r\n'),
                out_line1.strip('\r\n'), out_line2))
        else:
            dst.write(src_line)
        src_line = src.readline()

    # p.communicate()
    p.terminate()
    src.close()
    dst.close()


if __name__ == '__main__':
    main()