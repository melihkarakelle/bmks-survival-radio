#!/usr/bin/env python3
"""Pixel-honest UI proposal. Read-only firmware inputs; outputs beside this file.
Font bitmaps: firmware/font.c (Dual Tachyon, Apache-2.0); no substitute fonts.
Run: .venv/bin/python design/ui_v2/render_mockups.py
"""
from pathlib import Path
import re
import math
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = HERE / 'mockups'
BG, INK = '#AABE96', '#141E14'

def active_c(path):
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)
    out, stack, active = [], [], True
    for line in text.splitlines():
        s = line.strip()
        if s.startswith(('#if ', '#ifdef ', '#ifndef ')):
            cond = s == '#ifdef ENABLE_SMALL_BOLD' or s == '#if 1'
            if s not in ('#if 0', '#if 1', '#ifdef ENABLE_SMALL_BOLD'):
                raise ValueError(f'Unhandled preprocessor directive: {s}')
            stack.append([active, cond])
            active = active and cond
        elif s.startswith('#elif '):
            assert s == '#elif 0'
            active = False
        elif s == '#else':
            parent, taken = stack[-1]
            active = parent and not taken
            stack[-1][1] = True
        elif s == '#endif':
            active = stack.pop()[0]
        elif active:
            out.append(line)
    assert not stack
    return '\n'.join(out)

def array(source, name, n, width):
    match = re.search(r'\b' + name + r'\s*(?:\[[^]]+\])+\s*=\s*\{(.*?)\};', source, re.S)
    assert match, name
    rows = [[int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', row)]
            for row in re.findall(r'\{([^{}]+)\}', match.group(1))]
    assert len(rows) == n and all(len(r) == width for r in rows), (name, len(rows))
    return rows

src = active_c(ROOT / 'firmware/font.c')
SMALL = array(src, 'gFontSmall', 94, 6)
BOLD = array(src, 'gFontSmallBold', 94, 6)
BIG = array(src, 'gFontBig', 94, 14)
DIGITS = array(src, 'gFontBigDigits', 11, 20)
bitmap_src = re.sub(r'/\*.*?\*/|//[^\n]*', '', (ROOT / 'firmware/bitmaps.c').read_text(), flags=re.S)
LOGO = array(bitmap_src, 'BITMAP_SurvivalLogo', 5, 128)
frames = []

class LCD:
    def __init__(self, state='READY', locked=False):
        self.im = Image.new('1', (128, 64), 0)
        self.d = ImageDraw.Draw(self.im)
        self.text(state, 0, 0, bold=True)
        if locked:
            self.text('LOCK', 49, 0)
        self.text('8.2', 84, 0)
        self.d.rectangle((108, 1, 124, 6), outline=1)
        self.d.rectangle((125, 3, 126, 4), fill=1)
        for x in (110, 114, 118):
            self.d.rectangle((x, 3, x+1, 4), fill=1)

    def bytes(self, data, x, y, width):
        for i, byte in enumerate(data):
            px, py = x + i % width, y + (i // width)*8
            assert 0 <= px < 128 and 0 <= py <= 56
            for bit in range(8):
                if byte & (1 << bit):
                    self.im.putpixel((px, py+bit), 1)

    def text(self, s, x, y, big=False, bold=False):
        pitch = 8 if big else 7
        assert x >= 0 and x + len(s)*pitch <= 128, (s, x, pitch)
        assert y >= 0 and y + (16 if big else 8) <= 64, (s, y)
        font = BIG if big else BOLD if bold else SMALL
        for i, c in enumerate(s):
            assert 32 <= ord(c) <= 126
            if c != ' ':
                self.bytes(font[ord(c)-33], x+i*pitch+(0 if big else 1), y, 7 if big else 6)

    def row(self, r, s, **kw):
        self.text(s, 0, 8+r*8, **kw)

    def inv(self, r, end=None, x0=0, x1=127):
        for y in range(8+r*8, 8+(r+1 if end is None else end+1)*8):
            for x in range(x0, x1+1):
                self.im.putpixel((x, y), 0 if self.im.getpixel((x, y)) else 1)

    def freq(self, s, r=3):
        # Same 13 px digit advance, 10 stored columns, +2 inset and 3 px dot
        # as UI_DisplayFrequency. All 5 fractional digits fit: 107 px at 3 MHz digits.
        x, y = 0, 8+r*8
        if len(s.split('.')[0]) > 3:
            self.row(r, s, big=True)
            return
        for c in s:
            if c == '.':
                for dx in range(3):
                    self.bytes([0, 0x60], x+dx, y, 1)
                x += 3
            else:
                self.bytes(DIGITS[10 if c == '-' else int(c)], x+2, y, 10)
                x += 13
        assert x <= 128

    def save(self, name):
        frames.append((name, self.im.copy()))

def main(name, group, title, freq, detail, state='READY', hint='M:MENU  *:SCAN', pri=False, locked=False):
    a = LCD(state, locked)
    a.row(0, group, bold=True)
    if pri:
        a.text('PRI', 105, 8, bold=True)
    a.row(1, title[:16], big=True)
    a.freq(freq)
    a.row(5, detail)
    a.row(6, hint)
    if state in ('SCAN UP', 'SCAN DN', 'TX', 'RX'):
        a.inv(0)
    a.save(name)

# Boot retains the actual existing 128x40 bitmap.
a=LCD('BOOT'); a.bytes(sum(LOGO, []), 0, 8, 128)
a.row(5, 'RADIO vNEXT', bold=True); a.row(6, 'M:MENU  F:HELP'); a.save('01_boot')
main('02_pmr', 'PMR446', 'PMR 1', '446.00625', 'LOW4 NAR SQL1')
main('03_frs', 'FRS', 'FRS 1', '462.56250', 'LOW4 WIDE SQL1')
main('04_marine', 'MARINE', 'MAR 16', '156.80000', 'RX ONLY SQL1', pri=True)
main('05_ham', 'HAM', '2M CALL EU', '145.50000', 'RX ONLY SQL1')
main('06_cb', 'CB', 'CB 9', '27.06500', 'RX ONLY SQL1')
main('07_fav', 'FAV', 'CAMP BASE', '446.00625', 'LOW4 NAR SQL1', pri=True)
main('08_air', 'AIR', 'AIR BAND', '121.50000', 'AM 25K SQL1')
main('09_custom', 'CUSTOM', 'CUSTOM', '145.50000', 'FM 12.5K SQL1')
main('10_air_scan', 'AIR', 'AIR BAND', '123.4----', 'AM 25K SQL1', 'SCAN UP', '*:STOP UP/DN:STOP')
main('11_channel_scan', 'PMR446', 'PMR 5', '446.05625', 'LOW4 NAR SQL1', 'SCAN DN', '*:STOP UP/DN:STOP')
main('12_received', 'MARINE', 'MAR 16', '156.80000', 'RX ONLY SQL1', 'RX', 'M:MENU  *:SCAN', True)
main('13_transmit', 'PMR446', 'PMR 1', '446.00625', 'LOW4 NAR 00:12', 'TX', 'RELEASE PTT:STOP')
main('14_locked', 'PMR446', 'PMR 1', '446.00625', 'LOW4 NAR SQL1', locked=True, hint='HOLD F:UNLOCK')
main('15_custom_ghz', 'CUSTOM', 'CUSTOM', '1200.00000', 'FM 25K SQL1')

def fm(name, mode, bg='WATCH MAR 16', freq='156.80000 MHz'):
    a=LCD('FM'); a.row(0, 'FM RADIO', bold=True); a.freq('98.4', 1)
    a.row(3, mode); a.row(4, bg, bold=True); a.inv(4)
    a.row(5, freq); a.row(6, '*:STOP EXIT:STOP' if mode.startswith(('FIND', 'SEEK')) else 'M:MENU  *:SEEK'); a.save(name)
fm('16_fm_watch', 'SAVED 03')
fm('17_fm_scan', 'TUNE 87.5-108', 'SCAN AIR BAND', '123.4---- MHz')
fm('18_fm_autostore', 'FIND ALL 07/20', 'WATCH PMR 1', '446.00625 MHz')

def menu(name, prev, selected, nxt, title, value, desc='', edit=False, advanced=False):
    a=LCD('MENU')
    a.text('ADV' if advanced else 'BASIC', 0, 8, bold=True)
    a.text(prev, 0, 16)
    a.text(selected, 0, 24, big=True)
    a.text(nxt, 0, 40)
    a.inv(2,3,x1=47)
    a.d.line((48,8,48,55),fill=1)
    a.text(title,50,8,bold=True)
    a.text(value,50,24,big=len(value)<=9)
    a.text(desc,50,48)
    if edit:
        a.inv(2,3,x0=50)
    a.row(6, 'M:SAVE EXIT:CANCEL' if edit else 'M:EDIT EXIT:BACK')
    a.save(name)
menu('19_menu_basic','Prio','Quiet','Power','Squelch','1','0=OPEN')
menu('20_menu_edit','Prio','Quiet','Power','Squelch','2','0=OPEN',True)
menu('21_menu_advanced','Width','Step','RxTone','Tune step','8.33','kHz',advanced=True)
menu('22_menu_group','Help','Group','Heard','Use group','MARINE','9 GROUPS',True)
menu('23_menu_fm','Group','FmTune','FmSave','FM tuning','Saved','20 SLOTS')

def page(name, title, lines, footer='M:NEXT EXIT:BACK'):
    a=LCD(); a.row(0,title,bold=True); a.inv(0)
    for r,s in enumerate(lines,1): a.row(r,s)
    a.row(6,footer); a.save(name)
page('24_heard','HEARD 1/8', ['MAR 16         <1m','PMR 1           2m','145.50000       5m','CAMP BASE      12m','CB 9            1h'],'M:GO EXIT:BACK')
for y in range(16,24):
    for x in range(128):
        im=frames[-1][1]
        im.putpixel((x,y), 0 if im.getpixel((x,y)) else 1)
page('25_heard_empty','HEARD 0/8',['','','NOTHING HEARD YET','',''],'EXIT:BACK')
page('26_help_shortcuts','SHORTCUTS 1/3',['0 FM RADIO','4 HEARD','5 PRIORITY','7 FAVORITE','UP/DN SQUELCH'])
page('27_help_controls','KEY HELP 2/3',['M MENU','HOLD M NEXT GROUP','* SCAN / STOP','HOLD UP/DN SCAN','HOLD F LOCK'])
page('28_help_side','KEY HELP 3/3',['SIDE1 MONITOR','HOLD SIDE1 LIGHT','REPEAT HOLD: SOS','SIDE2 FM RADIO','HOLD SIDE2 SCAN'])

def message(name, title, body, footer='EXIT:BACK', sub=''):
    a=LCD(); a.row(0,'NOTICE',bold=True); a.inv(0)
    a.row(1,title,big=True); a.row(3,body,big=True)
    a.row(5,sub); a.row(6,footer); a.save(name)
message('29_fav_added','FAV ADDED','PMR 1')
message('30_priority','PRIORITY','MAR 16')
message('31_fav_empty','FAV EMPTY','ADD A CHANNEL','M:BACK F:HELP')
message('32_fav_full','FAV FULL','58 OF 58','EXIT:BACK','REMOVE ONE FIRST')
message('33_prio_vfo','SAVE TO FAV','FIRST','M:ADD EXIT:BACK','THEN SET PRIORITY')
message('34_tx_blocked','TX BLOCKED','RELEASE PTT','EXIT:BACK','RX ONLY')
message('35_confirm_delete','REMOVE FAV?','CAMP BASE','M:YES EXIT:NO')
message('36_confirm_reset','RESET ALL?','ERASES FAV','M:YES EXIT:NO','RESTORES DEFAULTS')
message('37_fm_save','SAVE FM 03?','98.4 MHz','M:YES EXIT:NO','UP/DN SLOT')
message('38_fm_delete','DELETE FM 03?','98.4 MHz','M:YES EXIT:NO')
page('39_frequency_entry','CUSTOM MHz',['','145.5----','','TYPE 8 DIGITS','EXIT:ERASE'],'M:TUNE HOLD X:BACK')
page('40_name_entry','RENAME FAV',['','CAMP BASE','','UP/DN LETTER','M:NEXT *:SAVE'],'EXIT:ERASE/BACK')
page('41_system_info','ABOUT',['BMKs vNEXT','SURVIVAL UI V2','BATTERY 8.20V','BUILD: PROPOSAL',''],'EXIT:BACK')
page('42_service','SERVICE',['F Lock: READ ONLY','BatCal: 8.20V','BatTyp: 1600mAh','CALIBRATION TOOLS',''],'M:EDIT EXIT:BACK')
message('43_fav_removed','FAV REMOVED','CAMP BASE')
message('44_priority_off','PRIORITY OFF','MAR 16')
message('45_tx_timeout','TX TIME LIMIT','RELEASE PTT','EXIT:BACK','TRY AGAIN AFTER')
page('46_fm_empty','FM SAVED',['','NO SAVED STATIONS','','FmFind: FIND ALL','FmTune: TUNE'],'M:MENU EXIT:BACK')

message('47_confirm_fm_find','FIND ALL FM?','REPLACE LIST','M:YES EXIT:NO')
message('48_confirm_wide_tx','WIDE TX?','BYPASS GROUPS','M:YES EXIT:NO','BLOCKS STILL APPLY')
page('49_help_fm','SHORTCUTS 1/3',['0 EXIT FM','4 HEARD','5 RADIO ONLY','7 RADIO ONLY','* FIND ALL FM'])
fm('50_fm_seek', 'SEEK UP')
# Service starts with its first item visibly selected.
service = next(im for name,im in frames if name == '42_service')
for y in range(16,24):
    for x in range(128):
        service.putpixel((x,y), 0 if service.getpixel((x,y)) else 1)

OUT.mkdir(exist_ok=True)
for name, im in frames:
    rgb = Image.new('RGB', im.size, BG)
    rgb.paste(INK, mask=im)
    rgb.resize((512,256),Image.Resampling.NEAREST).save(OUT / f'{name}.png')
# Captions also use the source bitmap font. Each tile is 128x80 before scaling.
cols, tilew, tileh = 4, 136, 88
sheet = Image.new('RGB',(cols*tilew, math.ceil(len(frames)/cols)*tileh),BG)
for i,(name, im) in enumerate(frames):
    x,y=(i%cols)*tilew,(i//cols)*tileh
    caption=LCD(); caption.im.paste(0,(0,0,128,64)); caption.text(name[:18],0,0)
    tile=Image.new('RGB',(128,80),BG)
    tile.paste(INK,(0,0,128,8),caption.im.crop((0,0,128,8)))
    tile.paste(INK,(0,12,128,76),im)
    sheet.paste(tile,(x+4,y+4))
sheet.resize((sheet.width*4,sheet.height*4),Image.Resampling.NEAREST).save(OUT/'sheet.png')
assert all(Image.open(OUT/f'{n}.png').size == (512,256) for n,_ in frames)
assert all(len(Image.open(OUT/f'{n}.png').getcolors())==2 for n,_ in frames)
print(f'Validated font arrays; rendered {len(frames)} screens + sheet, exact 4x, two LCD colours.')
