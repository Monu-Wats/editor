#include <iostream>
#include <tuple>
#include <cmath>
#include "interface.h"
#include "disk.h"

using namespace std;

namespace Inter {

const IO::Colour
	screenbg=IO::Colour(0,0,0),
	screenfg=IO::Colour(198,198,198),
	tabbg=IO::Colour(38,38,38),
	editbg=IO::Colour(38,38,38),
	textfg=IO::Colour(255,255,255),
	numberfg=IO::Colour(255,255,0);

vector<Filebuffer> buffers;

int frontBuffer=-1;


bool Filebuffer::cansave(void){
	return openpath.size()!=0;
}

Maybe<string> Filebuffer::save(void){
	return Disk::writeToFile(openpath,to_string(contents));
}

Maybe<string> Filebuffer::saveas(string fname){
	openpath=fname;
	return save();
}

bool Filebuffer::canopen(void){
	return true;
}

bool Filebuffer::open(string fname,bool doredraw){
	openpath=fname;
	Maybe<string> mcont=Disk::readFromFile(fname);
	if(mcont.isNothing())return false;
	contents=Textblob(mcont.fromJust());
	if(doredraw)Screen::redraw();
	return true;
}


Filebuffer& addfilebuffer(bool doredraw){
	frontBuffer++;
	buffers.emplace(buffers.begin()+frontBuffer);
	if(doredraw)Screen::redraw();
	return buffers[frontBuffer];
}

Filebuffer& addfilebufferfile(const string &fname,bool doredraw){
	frontBuffer++;
	buffers.emplace(buffers.begin()+frontBuffer);
	Filebuffer &buf=buffers[frontBuffer];
	buf.open(fname,false);
	if(doredraw)Screen::redraw();
	return buf;
}

void printStatus(string status,IO::Colour clr,bool bold){
	unsigned int scrwidth,scrheight;
	tie(scrwidth,scrheight)=IO::screensize();
	IO::gotoxy(0,scrheight-1);
	IO::switchColourFg(clr);
	IO::switchColourBg(Inter::screenbg);
	if(bold)IO::turnOnBold();
	if(status.size()>scrwidth){
		cout<<status.substr(0,scrwidth-3)<<"...";
	} else {
		cout<<status<<string(scrwidth-status.size(),' ');
	}
	if(bold)IO::clearMarkup();
	Screen::gotoFrontBufferCursor();
	cout.flush();
}

void clearStatus(void){
	unsigned int scrwidth,scrheight;
	tie(scrwidth,scrheight)=IO::screensize();
	IO::gotoxy(0,scrheight-1);
	IO::switchColourBg(Inter::screenbg);
	cout<<string(scrwidth,' ');
	Screen::gotoFrontBufferCursor();
	cout.flush();
}

void drawScreen(Screen::Screencell *screen,unsigned int width,unsigned int height){
	unsigned int tabwidth;
	if(buffers.size()*3-1>width)tabwidth=2;
	else tabwidth=UINT_MAX;
	unsigned int acclen=0;
	double factor=1;
	vector<string> basenames;
	basenames.reserve(buffers.size());
	int ndirty=0;
	for(const Filebuffer &buffer : buffers){
		basenames.push_back(basename(buffer.openpath));
		string &fname=basenames.back();
		if(fname.size()==0)basenames.back()="<>";
		if(acclen)acclen++; //space between tabs
		acclen+=fname.size()?fname.size():1;
		if(buffer.dirty){
			acclen++;
			ndirty++;
		}
	}
	if(acclen>width)factor=(width-(buffers.size()-1-ndirty))/acclen;
	//cerr<<"factor="<<factor<<endl;
	unsigned int x=0,y=0,linenum;
	unsigned int i,j;
	unsigned int nbuf=buffers.size();
	//for(i=0;i<nbuf;i++)cerr<<"basenames[i]="<<basenames[i]<<"  "; cerr<<endl;
	//cerr<<"textfg = "<<(int)textfg.r<<' '<<(int)textfg.g<<' '<<(int)textfg.b<<endl;
	for(i=0;i<nbuf;i++){
		if(i!=0){
			Screen::Screencell &cell=screen[width*y+x];
			cell.ch=' ';
			cell.clr.bg=screenbg;
			cell.clr.ul=false;
			x++;
		}
		const unsigned int bnlen=(unsigned int)(basenames[i].size()*factor);
		for(j=0;j<bnlen;j++,x++){
			Screen::Screencell &cell=screen[width*y+x];
			cell.ch=basenames[i][j];
			cell.clr.fg=screenfg;
			if((int)i==frontBuffer){
				cell.clr.fg=textfg;
				cell.clr.bg=tabbg;
			} else {
				cell.clr.bg=screenbg;
			}
			cell.clr.ul=false;
		}
		if(buffers[i].dirty){
			Screen::Screencell &cell=screen[width*y+x];
			cell.ch='*';
			cell.clr.fg=textfg;
			cell.clr.bg=tabbg;
			cell.clr.ul=false;
			x++;
		}
	}
	for(;x<width;x++){
		Screen::Screencell &cell=screen[width*y+x];
		cell.ch=' ';
		cell.clr.bg=screenbg;
	}

	y++; x=0;
	if(frontBuffer==-1){
		//addfilebuffer(false);
		Screen::fillRect(screen,width,0,y,width,height-y,{textfg,screenbg});
		return;
	}
	Filebuffer &fbuf=buffers[frontBuffer];
	const unsigned int numberlen=log10(max(fbuf.contents.numlines(),(size_t)1))+1;
	const unsigned int editx=numberlen+2;
	linenum=fbuf.scrolly;
	Screen::fillRect(screen,width,0,y,editx,height-y-1,{numberfg,editbg});
	Screen::fillRect(screen,width,editx,y,width-editx,height-y-1,{textfg,editbg});
	Screen::fillRect(screen,width,0,height-1,width,1,{textfg,screenbg});
	fbuf.screencurx=0;
	fbuf.screencury=height-1;
	for(;y<height-1&&linenum<fbuf.contents.numlines();y++,linenum++){
		int n=linenum+1;
		screen[width*y].ch=' ';
		for(x=editx-2;n;x--,n/=10)screen[width*y+x].ch='0'+n%10;
		x=editx;
		const string line=fbuf.contents.line(linenum);
		const size_t linelen=line.size();
		if(linelen==0&&linenum==fbuf.cury){
			fbuf.screencurx=editx;
			fbuf.screencury=y;
		}
		for(i=0;i<linelen;i++){
			if(linenum==fbuf.cury&&i==fbuf.curx){
				fbuf.screencurx=x;
				fbuf.screencury=y;
			}
			const string pretty=line[i]=='\t'?string((x-editx+4)/4*4-(x-editx),' '):Screen::prettychar(line[i]);
			size_t plen=pretty.size();
			if(x+pretty.size()>=width){
				y++;
				n=linenum+1;
				for(x=0;x<editx;x++)screen[width*y+x].ch=' ';
			}
			for(j=0;j<plen;j++,x++)
				screen[width*y+x].ch=pretty[j];
		}
		if(linenum==fbuf.cury&&fbuf.curx==linelen){
			fbuf.screencurx=x;
			fbuf.screencury=y;
		}
	}
}

}
