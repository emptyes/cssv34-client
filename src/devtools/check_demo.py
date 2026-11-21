#!/usr/bin/env python3
"""
Structural check of a CS:S v34 demo (.dem, demo protocol 3, network protocol 7).

Walks every frame of the file and every network message inside dem_signon /
dem_packet frames, using the message formats of this engine (common/netmessages.cpp)
as they are on the v34 wire, and parses the dem_datatables block (SendTable_WriteInfos
+ DataTable_WriteClassInfosBuffer). Use it after changes to demo recording to make
sure the output is still readable by the original v34 client:

    python3 check_demo.py some.dem [--verbose]

Exit code 0 when the demo is well formed, 1 otherwise. Content is not interpreted
(entities, string table entries, user messages are skipped by their length fields).
"""

import argparse
import collections
import struct
import sys

DEMO_HEADER = struct.Struct('<8sii260s260s260s260sfiii')
CMDINFO_SIZE = 76  # democmdinfo_t: flags + 6 vectors

DEM_SIGNON, DEM_PACKET, DEM_SYNCTICK, DEM_CONSOLECMD, DEM_USERCMD, DEM_DATATABLES, DEM_STOP, DEM_STRINGTABLES = range(1, 9)
DEM_NAMES = {1: 'signon', 2: 'packet', 3: 'synctick', 4: 'consolecmd', 5: 'usercmd',
             6: 'datatables', 7: 'stop', 8: 'stringtables'}

NETMSG_TYPE_BITS = 5
NETMSG_LENGTH_BITS = 11
LAST_V34_MESSAGE = 31  # svc_GetCvarValue; svc_CmdKeyValues (32) and svc_SetPauseTimed (33) don't exist in v34

MSG_NAMES = {
    0: 'net_NOP', 1: 'net_Disconnect', 2: 'net_File', 3: 'net_Tick', 4: 'net_StringCmd',
    5: 'net_SetConVar', 6: 'net_SignonState', 7: 'svc_Print', 8: 'svc_ServerInfo',
    9: 'svc_SendTable', 10: 'svc_ClassInfo', 11: 'svc_SetPause', 12: 'svc_CreateStringTable',
    13: 'svc_UpdateStringTable', 14: 'svc_VoiceInit', 15: 'svc_VoiceData', 17: 'svc_Sounds',
    18: 'svc_SetView', 19: 'svc_FixAngle', 20: 'svc_CrosshairAngle', 21: 'svc_BSPDecal',
    23: 'svc_UserMessage', 24: 'svc_EntityMessage', 25: 'svc_GameEvent',
    26: 'svc_PacketEntities', 27: 'svc_TempEntities', 28: 'svc_Prefetch', 29: 'svc_Menu',
    30: 'svc_GameEventList', 31: 'svc_GetCvarValue', 32: 'svc_CmdKeyValues', 33: 'svc_SetPauseTimed',
}

# dt_common.h / dt.h
DPT_ARRAY, DPT_DATATABLE = 4, 5
DPT_V34_TYPES = 6  # DPT_Int..DPT_DataTable
SPROP_EXCLUDE = 1 << 6
PROPINFOBITS_NUMPROPS, PROPINFOBITS_TYPE, PROPINFOBITS_FLAGS = 9, 5, 13
PROPINFOBITS_NUMBITS, PROPINFOBITS_NUMELEMENTS = 6, 10


class DemoError(Exception):
    pass


class BitReader:
    """Little-endian bit reader matching bf_read."""

    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.size = len(data) * 8

    def left(self):
        return self.size - self.pos

    def bits(self, count):
        if count == 0:
            return 0
        if self.pos + count > self.size:
            raise DemoError('read past the end of the buffer')
        first = self.pos >> 3
        shift = self.pos & 7
        value = int.from_bytes(self.data[first:first + ((shift + count + 7) >> 3)], 'little') >> shift
        self.pos += count
        return value & ((1 << count) - 1)

    def skip(self, count):
        if count < 0 or self.pos + count > self.size:
            raise DemoError('length field points past the end of the buffer')
        self.pos += count

    def string(self, limit=4096):
        out = bytearray()
        while True:
            c = self.bits(8)
            if c == 0:
                return bytes(out)
            out.append(c)
            if len(out) > limit:
                raise DemoError('unterminated string')

    def coord(self):
        has_int, has_frac = self.bits(1), self.bits(1)
        if has_int or has_frac:
            self.bits(1)  # sign
            if has_int:
                self.bits(14)
            if has_frac:
                self.bits(5)

    def vec3coord(self):
        flags = (self.bits(1), self.bits(1), self.bits(1))
        for present in flags:
            if present:
                self.coord()


def read_message(msg_type, r, info):
    """Reads one message body; formats from common/netmessages.cpp (v34 wire)."""
    if msg_type == 0:                       # net_NOP
        return
    if msg_type == 1:                       # net_Disconnect
        r.string()
    elif msg_type == 2:                     # net_File
        r.bits(32); r.string(); r.bits(1)
    elif msg_type == 3:                     # net_Tick (v34: tick only)
        r.bits(32)
    elif msg_type in (4, 7):                # net_StringCmd, svc_Print
        r.string()
    elif msg_type == 5:                     # net_SetConVar
        for _ in range(r.bits(8)):
            r.string(); r.string()
    elif msg_type == 6:                     # net_SignonState
        r.bits(8); r.bits(32)
    elif msg_type == 8:                     # svc_ServerInfo
        protocol = r.bits(16)
        r.bits(32); r.bits(1); r.bits(1); r.bits(32); r.bits(16); r.bits(32)
        r.bits(8); r.bits(8); r.bits(32); r.bits(8)
        info['serverinfo'] = (protocol, r.string(), r.string(), r.string(), r.string())
    elif msg_type == 9:                     # svc_SendTable
        r.bits(1); r.skip(r.bits(16))
    elif msg_type == 10:                    # svc_ClassInfo
        count = r.bits(16)
        if not r.bits(1):
            id_bits = count.bit_length()    # Q_log2( count ) + 1
            for _ in range(count):
                r.bits(id_bits); r.string(); r.string()
    elif msg_type == 11:                    # svc_SetPause
        r.bits(1)
    elif msg_type == 12:                    # svc_CreateStringTable
        name = r.string()
        max_entries = r.bits(16)
        if max_entries <= 0 or max_entries & (max_entries - 1):
            raise DemoError('string table %r: max entries %d is not a power of two' % (name, max_entries))
        r.bits(max_entries.bit_length())    # num entries: Q_log2( max ) + 1 bits
        length = r.bits(20)                 # NET_MAX_PAYLOAD_BITS_V23 + 3
        if r.bits(1):
            r.bits(12); r.bits(4)
        r.skip(length)
        info['tables'].append(name.decode('latin-1'))
    elif msg_type == 13:                    # svc_UpdateStringTable
        r.bits(5)
        if r.bits(1):
            r.bits(16)
        r.skip(r.bits(16))
    elif msg_type == 14:                    # svc_VoiceInit
        r.string(); r.bits(8)
    elif msg_type == 15:                    # svc_VoiceData
        r.bits(8); r.skip(r.bits(16))
    elif msg_type == 17:                    # svc_Sounds
        if r.bits(1):
            length = r.bits(8)
        else:
            r.bits(8); length = r.bits(16)
        r.skip(length)
    elif msg_type == 18:                    # svc_SetView
        r.bits(11)
    elif msg_type == 19:                    # svc_FixAngle
        r.bits(1); r.bits(48)
    elif msg_type == 20:                    # svc_CrosshairAngle
        r.bits(48)
    elif msg_type == 21:                    # svc_BSPDecal
        r.vec3coord(); r.bits(9)
        if r.bits(1):
            r.bits(11); r.bits(11)
        r.bits(1)
    elif msg_type == 23:                    # svc_UserMessage
        info['usermessages'][r.bits(8)] += 1
        r.skip(r.bits(NETMSG_LENGTH_BITS))
    elif msg_type == 24:                    # svc_EntityMessage
        r.bits(11); r.bits(9); r.skip(r.bits(NETMSG_LENGTH_BITS))
    elif msg_type == 25:                    # svc_GameEvent
        r.skip(r.bits(NETMSG_LENGTH_BITS))
    elif msg_type == 26:                    # svc_PacketEntities
        r.bits(11)
        if r.bits(1):
            r.bits(32)
        r.bits(1); r.bits(11)
        length = r.bits(20)                 # DELTASIZE_BITS
        r.bits(1)
        r.skip(length)
    elif msg_type == 27:                    # svc_TempEntities
        r.bits(8); r.skip(r.bits(17))
    elif msg_type == 28:                    # svc_Prefetch
        r.bits(13)
    elif msg_type == 29:                    # svc_Menu
        r.bits(16); r.skip(r.bits(16) * 8)
    elif msg_type == 30:                    # svc_GameEventList
        r.bits(9); r.skip(r.bits(20))
    elif msg_type == 31:                    # svc_GetCvarValue
        r.bits(32); r.string()
    else:
        raise DemoError('message type %d does not exist in v34' % msg_type)


def check_packet(payload, info):
    r = BitReader(payload)
    while r.left() >= NETMSG_TYPE_BITS:
        msg_type = r.bits(NETMSG_TYPE_BITS)
        info['messages'][MSG_NAMES.get(msg_type, 'unknown_%d' % msg_type)] += 1
        if msg_type > LAST_V34_MESSAGE:
            raise DemoError('%s does not exist in v34' % MSG_NAMES.get(msg_type, msg_type))
        read_message(msg_type, r, info)


def check_datatables(payload):
    r = BitReader(payload)
    tables = props = 0
    while r.bits(1):
        r.bits(1)  # needs decoder
        r.string()
        for _ in range(r.bits(PROPINFOBITS_NUMPROPS)):
            prop_type = r.bits(PROPINFOBITS_TYPE)
            if prop_type >= DPT_V34_TYPES:
                raise DemoError('send prop type %d does not exist in v34' % prop_type)
            r.string()
            flags = r.bits(PROPINFOBITS_FLAGS)
            if prop_type == DPT_DATATABLE or flags & SPROP_EXCLUDE:
                r.string()
            elif prop_type == DPT_ARRAY:
                r.bits(PROPINFOBITS_NUMELEMENTS)
            else:
                r.bits(32); r.bits(32); r.bits(PROPINFOBITS_NUMBITS)
            props += 1
        tables += 1
    classes = r.bits(16)
    for index in range(classes):
        class_id = r.bits(16)
        r.string(); r.string()
        if class_id != index:
            raise DemoError('class info %d has id %d' % (index, class_id))
    if r.left() >= 8:
        raise DemoError('%d unread bits after the class infos' % r.left())
    return tables, props, classes


def check_demo(path, verbose):
    data = open(path, 'rb').read()
    if len(data) < DEMO_HEADER.size:
        raise DemoError('file is shorter than the demo header')

    header = DEMO_HEADER.unpack_from(data, 0)
    magic, demo_protocol, net_protocol = header[0].split(b'\0')[0], header[1], header[2]
    text = [h.split(b'\0')[0].decode('utf-8', 'replace') for h in header[3:7]]
    playback_time, header_ticks, header_frames, signon_length = header[7:]
    print('demo: %s, demo protocol %d, network protocol %d' % (magic.decode('latin-1'), demo_protocol, net_protocol))
    print('server %s, client %s, map %s, game %s' % tuple(text))
    print('header: %.1f s, %d ticks, %d frames, signon %d bytes' % (playback_time, header_ticks, header_frames, signon_length))
    if magic != b'HL2DEMO' or demo_protocol != 3 or net_protocol != 7:
        raise DemoError('not a v34 demo (expected HL2DEMO, demo protocol 3, network protocol 7)')

    info = {'messages': collections.Counter(), 'usermessages': collections.Counter(), 'tables': []}
    commands = collections.Counter()
    offset = DEMO_HEADER.size
    last_tick = 0
    packets = 0
    stopped = False

    def need(count):
        if offset + count > len(data):
            raise DemoError('frame at offset %d is truncated' % offset)

    while offset < len(data):
        need(5)
        cmd = data[offset]
        tick = struct.unpack_from('<i', data, offset + 1)[0]
        frame_offset = offset
        offset += 5
        commands[DEM_NAMES.get(cmd, 'unknown_%d' % cmd)] += 1
        try:
            if cmd in (DEM_SIGNON, DEM_PACKET):
                need(CMDINFO_SIZE + 12)
                offset += CMDINFO_SIZE + 8
                length = struct.unpack_from('<i', data, offset)[0]
                offset += 4
                need(length)
                check_packet(data[offset:offset + length], info)
                offset += length
                if cmd == DEM_PACKET:
                    packets += 1
                    if tick < last_tick:
                        raise DemoError('tick goes back from %d to %d' % (last_tick, tick))
                    last_tick = tick
            elif cmd == DEM_SYNCTICK:
                pass
            elif cmd in (DEM_CONSOLECMD, DEM_DATATABLES):
                need(4)
                length = struct.unpack_from('<i', data, offset)[0]
                offset += 4
                need(length)
                if cmd == DEM_DATATABLES:
                    tables, props, classes = check_datatables(data[offset:offset + length])
                    print('datatables: %d send tables, %d props, %d classes' % (tables, props, classes))
                offset += length
            elif cmd == DEM_USERCMD:
                need(8)
                length = struct.unpack_from('<i', data, offset + 4)[0]
                offset += 8
                need(length)
                offset += length
            elif cmd == DEM_STRINGTABLES:
                raise DemoError('dem_stringtables does not exist in v34 demos')
            elif cmd == DEM_STOP:
                stopped = True
                break
            else:
                raise DemoError('unknown demo command %d' % cmd)
        except DemoError as e:
            raise DemoError('%s frame at offset %d, tick %d: %s' % (DEM_NAMES.get(cmd, cmd), frame_offset, tick, e))

    if not stopped:
        raise DemoError('no dem_stop: the demo was not finished properly')
    if offset != len(data):
        print('warning: %d bytes after dem_stop' % (len(data) - offset))
    if header_frames != packets:
        print('warning: header says %d frames, file has %d packets' % (header_frames, packets))
    if header_ticks != last_tick:
        print('warning: header says %d ticks, last packet is at tick %d' % (header_ticks, last_tick))

    print('frames: %s' % ', '.join('%s %d' % kv for kv in sorted(commands.items())))
    if 'serverinfo' in info:
        protocol, game, map_name, sky, host = info['serverinfo']
        print('svc_ServerInfo: protocol %d, %s, %s, host "%s"' % (protocol, game.decode('latin-1'),
              map_name.decode('latin-1'), host.decode('utf-8', 'replace')))
    print('string tables: %s' % ', '.join(info['tables']))
    if verbose:
        for name, count in sorted(info['messages'].items(), key=lambda kv: -kv[1]):
            print('  %-24s %d' % (name, count))
        print('user messages by id: %s' % dict(sorted(info['usermessages'].items())))
    print('OK: %d messages in %d signon/packet frames' % (sum(info['messages'].values()),
          commands['signon'] + commands['packet']))


def main():
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument('demo')
    parser.add_argument('--verbose', '-v', action='store_true', help='print message statistics')
    args = parser.parse_args()
    try:
        check_demo(args.demo, args.verbose)
    except (DemoError, OSError) as e:
        print('ERROR: %s' % e)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
