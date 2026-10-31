import re
from parse import parse_rcmb   # your existing MOST decoder
from colorama import Fore, Style

def pretty(msg):
    src = msg['source']
    fb  = f"{msg['fblock_name']}({msg['fblock_id']})"
    inst = msg['inst_id']
    fkt = msg['fkt_name'] if msg['fkt_name'] != "Unknown" else f"Fkt {msg['fkt_id']}"
    op  = msg['op_name']
    at  = msg['addr_type']

    if msg['payload']:
        payload = " ".join(f"{b:02X}" for b in msg['payload'])
    else:
        payload = "—"

    # Color coding
    color = {
        'Logical': Fore.CYAN,
        'Physical': Fore.GREEN,
        'Broadcast': Fore.MAGENTA,
        'Groupcast': Fore.YELLOW
    }.get(at, Fore.WHITE)

    print(
        f"{color}📡 {src} → "
        f"{fb}[{inst}] . {fkt:<12} "
        f"{Style.RESET_ALL}({at}/{op})   "
        f"| Payload: {payload}"
    )

# Regex extracts exactly 20 bytes after `data=`
PATTERN = re.compile(r"data=(?:0x)?([0-9A-Fa-f]{2}(?:\s+[0-9A-Fa-f]{2}){19})")

def parse_log_file(path):
    with open(path) as f:
        for line in f:
            if "os8104 spi0.0: rreg 0xa0 len=20" in line:
                m = PATTERN.search(line)
                if m:
                    hex_bytes = m.group(1).split()
                    frame = bytes(int(b,16) for b in hex_bytes)

                    # Decode + print result
                    pretty(parse_rcmb(frame))


# ---------------- Run it ----------------
parse_log_file("../dmesg.log")
