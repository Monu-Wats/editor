#include <vector>
#include <termio.h>
#include "interface.h"
#include "diskio.h"
#include "editor.h"
#include "maybe.h"
#include "onelinebufferview.h"
#include "wrappingbufferview.h"

using namespace std;


namespace Interface{

	class TermioRAII{
	public:
		TermioRAII(){
			initscreen();
			initkeyboard(true);
			installCLhandler(true);
		}

		~TermioRAII(){
			endkeyboard();
			endscreen();
		}
	};

	Style statusbarStyle={.fg=9,.bg=9,.bold=true,.ul=false};
	Style errorStyle={.fg=1,.bg=9,.bold=true,.ul=false};

	static void clearStatus(){
		Size termsize=gettermsize();
		fillrect(0,termsize.h-1,termsize.w,1,' ');
	}

	static void printStatus(const string &str,Style *style=&statusbarStyle){
		clearStatus();
		Size termsize=gettermsize();
		pushcursor();
		moveto(0,termsize.h-1);
		setstyle(style);
		tprintf("%s",str.data());
		popcursor();
	}

	static bool confirmStatus(const string &str,bool defChoice){
		Size termsize=gettermsize();
		pushcursor();
		moveto(0,termsize.h-1);
		setstyle(&statusbarStyle);
		tprintf("%s [%s] ",str.data(),defChoice?"Y/n":"y/N");
		redraw();
		int key=tgetkey();
		bool choice;
		if(defChoice==true){
			choice=key!='n'&&key!='N';
		} else {
			choice=key=='Y'||key=='y';
		}
		fillrect(0,termsize.h-1,termsize.w,1,' ');
		redraw();
		popcursor();
		return choice;
	}

	static Maybe<string> askStatus(const string &prompt,const string &defvalue){
		Size termsize=gettermsize();
		pushcursor();
		moveto(0,termsize.h-1);
		setstyle(&statusbarStyle);
		i64 offset=tprintf("%s ",prompt.data());
		OnelineBufferView lineView(offset,termsize.h-1,termsize.w-offset);
		lineView.setText(defvalue);
		bool submit;
		while(true){
			lineView.draw();
			redraw();
			int key=tgetkey();
			if(key==KEY_CR||key==KEY_LF){submit=true; break;}
			else if(key==KEY_ESC){submit=false; break;}
			if(!lineView.handleKey(key))bel();
		}
		popcursor();
		if(submit)return {lineView.fullText()};
		else return {};
	}

	static void saveView(WrappingBufferView &view,bool askName){
		if(askName){
			Maybe<string> mname=askStatus("Save:",view.getName());
			if(mname.isJust()){
				view.setName(mname.fromJust());
			} else {
				return;
			}
		}

		const string &fname=view.getName();
		try {
			DiskIO::writeFile(fname,view.fullText());
			printStatus("Saved to '"+fname+"'");
			view.setClean();
		} catch(DiskIO::DiskError e){
			printStatus(e.what(),&errorStyle);
		}
	}

	static string getTabName(const WrappingBufferView &view){
		string name=view.getName().size()==0 ? "<New File>" : view.getName();
		if(view.isDirty()){
			name+="*";
		}
		return name;
	}

	static bool closeViewCheck(Editor &editor,i64 index){
		if(editor.view(index).isDirty()&&!confirmStatus("Unsaved changes. Close buffer?",false)){
			return false;
		}
		editor.closeView(index);
		return true;
	}

	void show(){
		TermioRAII termioRAII;
		i64 tabscroll=0;
		Editor editor([](i64 &x,i64 &y,i64 &w,i64 &h){
			Size termsize=gettermsize();
			x=0; y=1;
			w=termsize.w; h=termsize.h-2;
		});

		while(true){
			Size termsize=gettermsize();

			i64 nviews=editor.numViews();
			i64 tabpos[nviews],tabwid[nviews];
			vector<string> tabname;
			for(i64 i=0;i<nviews;i++){
				tabname.push_back(getTabName(editor.view(i)));
			}
			tabpos[0]=0;
			tabwid[0]=min((i64)tabname[0].size()+2,(i64)termsize.w-4);
			for(i64 i=1;i<nviews;i++){
				tabpos[i]=tabpos[i-1]+tabwid[i-1];
				tabwid[i]=min((i64)tabname[i].size()+2,(i64)termsize.w-4);
			}

			i64 activeidx=editor.activeIndex();

			if(tabpos[activeidx]-tabscroll<0){
				tabscroll=max(tabpos[activeidx]-1,0LL);
			} else if(tabpos[activeidx]+tabwid[activeidx]-tabscroll>=termsize.w){
				tabscroll=max(tabpos[activeidx]+tabwid[activeidx]-(termsize.w-2),0LL);
			}

			moveto(0,0);
			Style blankStyle={.fg=9,.bg=9,.bold=false,.ul=false};
			setstyle(&blankStyle);
			fillrect(0,0,termsize.w,1,' ');

			for(i64 i=0;i<nviews;i++){
				Style style={.fg=7,.bg=i==activeidx?4:9,.bold=false,.ul=false};
				setstyle(&style);
				const string &name=tabname[i];
				if(tabpos[i]<tabscroll&&tabpos[i]+tabwid[i]>=tabscroll){
					if(tabpos[i]+tabwid[i]==tabscroll){
						tputc(' ');
					} else {
						for(i64 j=tabscroll-tabpos[i]-1;j<tabwid[i];j++){
							tputc(name[j]);
						}
						tputc(' ');
					}
				} else if(tabpos[i]>=tabscroll&&tabpos[i]+tabwid[i]<tabscroll+termsize.w){
					tputc(' ');
					for(i64 j=0;j<tabwid[i]-2;j++){
						tputc(name[j]);
					}
					tputc(' ');
				} else if(tabpos[i]<tabscroll+termsize.w&&tabpos[i]+tabwid[i]>=tabscroll+termsize.w){
					tputc(' ');
					for(i64 j=0;j<tabscroll+termsize.w-tabpos[i]-1;j++){
						tputc(name[j]);
					}
					break;
				}
			}

			editor.drawActive();

			redraw();

			int key=tgetkey();
			cerr<<"tgetkey -> "<<key<<endl;
			switch(key){
				case KEY_CTRL+'Q':
					if(confirmStatus("Quit editor?",true)){
						while(editor.numViews()>1){
							if(!closeViewCheck(editor,0)){
								editor.setActiveIndex(0);
								break;
							}
						}
						if(!closeViewCheck(editor,0)){ // close the last one
							editor.setActiveIndex(0);
							break;
						}
						return;
					}
					break;

				case KEY_CTRL+'N':
					editor.newView();
					break;

				case KEY_CTRL+'W':
					closeViewCheck(editor,activeidx);
					break;

				case KEY_CTRL+'S':
					saveView(editor.view(activeidx),editor.view(activeidx).getName().size()==0);
					break;

				case KEY_CTRLALT+'S':
					saveView(editor.view(activeidx),true);
					break;

				case KEY_ALT+KEY_TAB:
					editor.setActiveIndex((editor.activeIndex()+1)%editor.numViews());
					break;

				case KEY_ALT+KEY_SHIFTTAB:
					editor.setActiveIndex((editor.activeIndex()+editor.numViews()-1)%editor.numViews());
					break;

				default:
					if(!editor.handleKey(key)){
						printStatus("Unknown command");
						bel();
					}
					break;
			}
		}
	}

}