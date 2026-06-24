#!/usr/bin/env python3
"""Test 11: User ring compaction on BoringTechH743.
"""
import os, termios, time, select, json, binascii, glob, sys

DEV = next(iter(glob.glob('/dev/serial/by-id/usb-ThetaGamepad*if01*')), '/dev/ttyACM1')
print(f'Device: {DEV}')

def open_dev():
    fd = os.open(DEV, os.O_RDWR | os.O_NOCTTY)
    a = termios.tcgetattr(fd)
    for i in range(4):
        if i == 0: a[i] &= ~(termios.IGNBRK|termios.BRKINT|termios.PARMRK|termios.ISTRIP|termios.INLCR|termios.IGNCR|termios.ICRNL|termios.IXON)
        elif i == 1: a[i] &= ~termios.OPOST
        elif i == 2: a[i] = (a[i] & ~(termios.CSIZE|termios.PARENB|termios.CSTOPB)) | termios.CS8
        else: a[i] &= ~(termios.ECHO|termios.ECHONL|termios.ICANON|termios.ISIG|termios.IEXTEN)
    a[4] = a[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, a)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd

def rl(fd, t=5):
    buf = b''
    while True:
        r,_,_ = select.select([fd],[],[],t)
        if not r: return None
        ch = os.read(fd, 1)
        if not ch: return None
        if ch == b'\n': return buf.decode().strip()
        if ch != b'\r': buf += ch

def send(fd, cmd, **kw):
    body = json.dumps({'cmd':cmd,'queued':0,**kw}, separators=(',',':'))
    os.write(fd, (body + '\r\n').encode())
    for _ in range(120):
        r = rl(fd)
        if r is None: continue
        try:
            o = json.loads(r)
            if o.get('status') == 'async': continue
            if o.get('cmd') != cmd: continue
            return o
        except: continue
    return None

print('=' * 50)
print('TEST 11: User Ring Compaction')
print('=' * 50)

# Clean the SPI flash
fd = open_dev()
print('1. chip_erase...', flush=True)
r = send(fd, 'test.chip_erase')
print(f'   chip_erase: {r}')

# Wait for erase to complete
os.close(fd)
print('   waiting 20s...', flush=True)
time.sleep(20)

# Reconnect and check state
fd = open_dev()
r = send(fd, 'profile.status')
print(f'2. After erase: {r}')
has_init = r.get('profile_count', 0) > 0

# If count > 0, we need to delete existing profiles
need_clean = r.get('active_profile_id', 0) == 15 or r.get('profile_count', 0) > 1
if need_clean:
    print('   cleaning old profiles...')
    for pid in range(1, 16):  # don't delete Profile0
        send(fd, 'profile.delete', id=pid)
    for pid in range(1, 16):
        r = send(fd, 'profile.status')
        if r.get('profile_count', 0) <= 1:
            break
    r = send(fd, 'profile.status')
    print(f'   cleaned: {r}')

start = time.time()
total = 0
compaction = False
fails = 0
last_addr = r.get('next_addr', 0)
free_start = r.get('free_sectors', 2046)

print(f'\n3. Filling user ring...')

# Phase A: Create profiles 1-15 to fill all user slots
for pid in range(1, 16):
    h = binascii.hexlify(b'{"map":{"socd":' + str(pid).encode() + b'}}').decode()
    resp = send(fd, 'profile.create', data_hex=h)
    if not resp or resp.get('status') != 'ok':
        print(f'   create {pid} failed: {resp}')
        sys.exit(1)
    total += 1

# Status check
r = send(fd, 'profile.status')
print(f'   Created 15 profiles: {r.get("profile_count")} profiles, next=0x{r.get("next_addr"):x}')

# Phase B: Rotate profiles to fill sectors
last_addr = r.get('next_addr', 0)
to_delete = list(range(1, 16))

while True:
    for pid in to_delete:
        # Delete
        r = send(fd, 'profile.delete', id=pid)
        if not r or r.get('status') != 'ok':
            fails += 1
            continue
        # Create new (will reuse same ID)
        h = binascii.hexlify(b'{"map":{"socd":' + str(total % 256).encode() + b'}}').decode()
        r = send(fd, 'profile.create', data_hex=h)
        if not r or r.get('status') != 'ok':
            fails += 1
            if fails > 10:
                print(f'   FAIL: {fails} consecutive failures at profile {total}')
                sys.exit(1)
            continue
        total += 1
        fails = 0
    
    # Check state every full rotation
    r = send(fd, 'profile.status')
    addr = r.get('next_addr', 0)
    free = r.get('free_sectors', 0)
    used = r.get('used_sectors', 0)
    
    # Compaction: next_addr went backward
    if addr < last_addr and total > 20:
        compaction = True
        elapsed = time.time() - start
        print(f'\n   🔄 COMPACTION at profile #{total}! 0x{last_addr:x}→0x{addr:x}')
        break
    
    # Full
    if free == 0:
        elapsed = time.time() - start
        print(f'\n   ⬜ All sectors used at #{total}')
        break
    
    last_addr = addr
    
    if total % 200 == 0:
        elapsed = time.time() - start
        rate = total / elapsed
        pct = (1 - free / free_start) * 100 if free_start else 0
        print(f'   [{total:5d}] 0x{addr:x} used={used} free={free} ({pct:.0f}%) {rate:.1f}c/s', flush=True)

# Summary
elapsed = time.time() - start
r = send(fd, 'profile.status')
r2 = send(fd, 'profile.list')
ids = [p['id'] for p in r2.get('profiles', [])] if r2 else []
ids_str = f'{len(ids)} profiles' if len(ids) > 5 else str(ids)
print(f'\n  Final: free={r.get("free_sectors")}/{r.get("total_sectors")} next=0x{r.get("next_addr"):x}')
print(f'  {ids_str}')
print(f'  {total}c in {elapsed:.0f}s ({total/elapsed:.1f}/s)')

ok = compaction or (r and r.get('free_sectors', 1) == 0)
print(f'\n  {"✅ Test 11 PASS" if ok else "❌ INCONCLUSIVE"}')

print(f'\n{"="*50}')
os.close(fd)
