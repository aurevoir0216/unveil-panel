from fontTools.ttLib import TTFont
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.t2CharStringPen import T2CharStringPen
from fontTools.pens.qu2cuPen import Qu2CuPen
import cffsubr
from pathlib import Path
import argparse
parser=argparse.ArgumentParser(description='Compact the Wi-Fi font, preserving Unicode coverage and advance widths.')
parser.add_argument('source',type=Path);parser.add_argument('output',type=Path)
args=parser.parse_args();source=args.source
f=TTFont(source);glyphs=f.getGlyphSet();order=f.getGlyphOrder();cmap=f.getBestCmap();metrics=f['hmtx'].metrics
fb=FontBuilder(f['head'].unitsPerEm,isTTF=False);fb.setupGlyphOrder(order);fb.setupCharacterMap(cmap)
strings={}
for name in order:
 pen=T2CharStringPen(metrics[name][0],glyphs);glyphs[name].draw(Qu2CuPen(pen,max_err=1.0,all_cubic=True));strings[name]=pen.getCharString(private=None,globalSubrs=None)
fb.setupCFF('UnveilWifi-Regular',{'Notice':f['name'].getDebugName(0) or '', 'FullName':'Unveil Wifi Regular','FamilyName':'Unveil Wifi','Weight':'Regular'},strings,{})
fb.setupHorizontalMetrics(metrics);fb.setupHorizontalHeader(ascent=f['hhea'].ascent,descent=f['hhea'].descent,lineGap=f['hhea'].lineGap)
fb.setupNameTable({'copyright':f['name'].getDebugName(0) or '', 'familyName':'Unveil Wifi','styleName':'Regular','uniqueFontIdentifier':'Unveil Wifi CFF 0.21','fullName':'Unveil Wifi Regular','psName':'UnveilWifi-Regular','version':'Version 0.21','licenseDescription':f['name'].getDebugName(13) or 'SIL Open Font License 1.1','licenseInfoURL':'https://openfontlicense.org/'})
os2=f['OS/2'];fb.setupOS2(sTypoAscender=os2.sTypoAscender,sTypoDescender=os2.sTypoDescender,sTypoLineGap=os2.sTypoLineGap,usWinAscent=os2.usWinAscent,usWinDescent=os2.usWinDescent,usWeightClass=os2.usWeightClass,usWidthClass=os2.usWidthClass)
fb.setupPost();target=args.output;cffsubr.subroutinize(fb.font);fb.save(target)
g=TTFont(target);assert g.getBestCmap()==cmap
assert g['hmtx'].metrics==metrics
print('characters',len(cmap),'source',source.stat().st_size,'CFF',target.stat().st_size)
