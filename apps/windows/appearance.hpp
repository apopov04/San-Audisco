#pragma once
// Native HWND controls keep their input, accessibility and notification behavior.
// Only their presentation is replaced; all drawing uses Windows system libraries.
#include <gdiplus.h>
#include <uxtheme.h>
#include <dwmapi.h>

namespace appearance {
struct Palette {
 bool dark=false,highContrast=false;
 COLORREF background{},surface{},hover{},pressed{},text{},secondary{},disabled{},divider{},accent{},selected{},danger{},thumb{};
 HBRUSH backgroundBrush{},surfaceBrush{};
} inline palette;
inline int preference=0; // 0 system, 1 light, 2 dark
inline int testTheme=-1;
inline Gdiplus::Color color(COLORREF c){return Gdiplus::Color(255,GetRValue(c),GetGValue(c),GetBValue(c));}
inline void graphics_ready(){struct Session{ULONG_PTR token{};Session(){Gdiplus::GdiplusStartupInput in;Gdiplus::GdiplusStartup(&token,&in,nullptr);}~Session(){if(token)Gdiplus::GdiplusShutdown(token);}};static Session session;}
inline bool refresh(bool force=false){
 HIGHCONTRASTW hc{sizeof(hc),0,nullptr};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0);
 DWORD light=1,bytes=sizeof(light);RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);
 bool dark=preference?preference==2:light==0;if(testTheme>=0)dark=testTheme!=0;
 bool high=(hc.dwFlags&HCF_HIGHCONTRASTON)!=0;
 if(!force&&palette.backgroundBrush&&palette.dark==dark&&palette.highContrast==high)return false;
 if(palette.backgroundBrush)DeleteObject(palette.backgroundBrush);if(palette.surfaceBrush)DeleteObject(palette.surfaceBrush);
 palette.dark=dark;palette.highContrast=high;
 if(high){palette.background=GetSysColor(COLOR_WINDOW);palette.surface=GetSysColor(COLOR_BTNFACE);palette.hover=palette.pressed=GetSysColor(COLOR_HIGHLIGHT);palette.text=GetSysColor(COLOR_WINDOWTEXT);palette.secondary=palette.text;palette.disabled=GetSysColor(COLOR_GRAYTEXT);palette.divider=palette.text;palette.accent=GetSysColor(COLOR_HIGHLIGHT);palette.selected=palette.surface;palette.danger=palette.text;palette.thumb=palette.text;}
 else if(dark){palette.background=RGB(40,40,40);palette.surface=RGB(55,55,55);palette.hover=RGB(66,66,66);palette.pressed=RGB(78,78,78);palette.text=RGB(237,237,237);palette.secondary=RGB(174,174,174);palette.disabled=RGB(132,132,132);palette.divider=RGB(66,66,66);palette.accent=RGB(10,132,255);palette.selected=RGB(40,52,67);palette.danger=RGB(255,83,91);palette.thumb=RGB(222,222,222);}
 else{palette.background=RGB(246,246,246);palette.surface=RGB(255,255,255);palette.hover=RGB(237,237,237);palette.pressed=RGB(221,221,221);palette.text=RGB(31,31,31);palette.secondary=RGB(103,103,103);palette.disabled=RGB(140,140,140);palette.divider=RGB(218,218,218);palette.accent=RGB(0,112,235);palette.selected=RGB(222,235,252);palette.danger=RGB(204,38,51);palette.thumb=RGB(255,255,255);}
 palette.backgroundBrush=CreateSolidBrush(palette.background);palette.surfaceBrush=CreateSolidBrush(palette.surface);return true;
}
inline void round_path(Gdiplus::GraphicsPath& path,float x,float y,float w,float h,float radius){
 float d=std::min(radius*2,std::min(w,h));if(w<=0||h<=0)return;
 path.AddArc(x,y,d,d,180,90);path.AddArc(x+w-d,y,d,d,270,90);path.AddArc(x+w-d,y+h-d,d,d,0,90);path.AddArc(x,y+h-d,d,d,90,90);path.CloseFigure();
}
inline void rounded(Gdiplus::Graphics& g,float x,float y,float w,float h,float radius,COLORREF fill){Gdiplus::GraphicsPath path;round_path(path,x,y,w,h,radius);Gdiplus::SolidBrush brush(color(fill));g.FillPath(&brush,&path);}
inline void outline(Gdiplus::Graphics& g,float x,float y,float w,float h,float radius,COLORREF ink,float thickness){Gdiplus::GraphicsPath path;round_path(path,x,y,w,h,radius);Gdiplus::Pen pen(color(ink),thickness);g.DrawPath(&pen,&path);}
inline float scale(HWND h){auto value=reinterpret_cast<INT_PTR>(GetPropW(h,L"SanAudisco.Dpi"));return (value?value:96)/96.f;}
inline std::wstring label(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,L'\0');GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
inline void text(HDC dc,HWND h,RECT r,const std::wstring& caption,COLORREF ink,UINT flags){auto previous=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0)));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,ink);DrawTextW(dc,caption.c_str(),static_cast<int>(caption.size()),&r,flags|DT_NOPREFIX);if(previous)SelectObject(dc,previous);}
inline bool audio_button(int id){return id==411||id==412||id==421||id==422||id==440||id==441||id==702||id==704||id==722||id==724;}
inline bool hub_button(int id){return id==702||id==704||id==722||id==724;}
inline void audio_glyph(Gdiplus::Graphics& g,float x,float y,float size,bool headphones,bool blocked,bool crown,COLORREF ink){
 auto saved=g.Save();g.TranslateTransform(x,y);g.ScaleTransform(size/32,size/32);Gdiplus::Pen pen(color(ink),1.9f);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);pen.SetLineJoin(Gdiplus::LineJoinRound);
 if(crown){Gdiplus::PointF points[]{{9,7},{7,1},{12,4},{16,1},{20,4},{25,1},{23,7},{9,7}};g.DrawLines(&pen,points,8);g.TranslateTransform(3,10);g.ScaleTransform(.82f,.72f);}
 if(headphones){g.DrawArc(&pen,5,4,22,24,180,180);Gdiplus::GraphicsPath left,right;round_path(left,4,16,6,12,3);round_path(right,22,16,6,12,3);g.DrawPath(&pen,&left);g.DrawPath(&pen,&right);}
 else{Gdiplus::GraphicsPath mic;round_path(mic,12,3,8,17,4);g.DrawPath(&pen,&mic);g.DrawArc(&pen,7,10,18,15,0,180);g.DrawLine(&pen,7.f,13.f,7.f,17.f);g.DrawLine(&pen,25.f,13.f,25.f,17.f);g.DrawLine(&pen,16.f,25.f,16.f,29.f);g.DrawLine(&pen,11.f,29.f,21.f,29.f);}
 if(blocked&&!crown)g.DrawLine(&pen,4.f,29.f,28.f,3.f);g.Restore(saved);
}
inline COLORREF static_ink(HWND h){int id=GetDlgCtrlID(h);if(!IsWindowEnabled(h))return palette.disabled;if(id==752)return palette.danger;if(id==751)return palette.dark?RGB(255,164,66):RGB(150,80,12);if(id==740||id==701||id==721||id==754||id==750||id==755||id==706||id==707||id==726||id==727)return palette.secondary;return palette.text;}
inline bool paint(HWND h,HDC dc){
 if(palette.highContrast&&!GetPropW(h,L"SanAudisco.Divider"))return false;graphics_ready();RECT r{};GetClientRect(h,&r);float w=float(r.right),height=float(r.bottom),s=scale(h);if(w<=0||height<=0)return true;
 wchar_t name[64]{};GetClassNameW(h,name,64);std::wstring cls=name;int id=GetDlgCtrlID(h);auto style=GetWindowLongPtrW(h,GWL_STYLE);
 if(cls==L"Edit")return false;
 FillRect(dc,&r,palette.backgroundBrush);if(GetPropW(h,L"SanAudisco.Divider")){auto brush=CreateSolidBrush(palette.divider);FillRect(dc,&r,brush);DeleteObject(brush);return true;}Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
 bool enabled=IsWindowEnabled(h),hot=GetPropW(h,L"SanAudisco.Hover")!=nullptr,focus=GetFocus()==h&&(SendMessageW(h,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)==0;
 if(cls==L"Button"){
  auto kind=style&BS_TYPEMASK;bool push=(style&BS_PUSHLIKE)||kind==BS_PUSHBUTTON||kind==BS_DEFPUSHBUTTON;bool checked=SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED;bool pressed=(SendMessageW(h,BM_GETSTATE,0,0)&BST_PUSHED)!=0;
  bool icon=audio_button(id),hub=hub_button(id),primary=id==601||id==205||id==307,link=id==606||id==607;
  COLORREF ink=!enabled?palette.disabled:primary?RGB(255,255,255):checked?(hub?palette.accent:palette.danger):palette.text;
  if(push){if(!link){COLORREF fill=primary?(enabled?palette.accent:palette.surface):checked&&hub?palette.selected:pressed?palette.pressed:hot&&enabled?palette.hover:palette.surface;rounded(g,1,1,w-2,height-2,6*s,fill);if(!palette.dark&&!primary)outline(g,1,1,w-2,height-2,6*s,palette.divider,.6f*s);}
   if(icon){float size=(hub?32:28)*s;audio_glyph(g,(w-size)/2,(height-size)/2,size,id==412||id==422||id==441||id==702||id==722,checked,hub,ink);}
   else{RECT bounds=r;if(link){bounds.left+=int(18*s);Gdiplus::Pen p(color(ink),1.6f*s);g.DrawLine(&p,5*s,height/2-4*s,9*s,height/2);g.DrawLine(&p,9*s,height/2,5*s,height/2+4*s);}text(dc,h,bounds,label(h),ink,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|(link?DT_LEFT:DT_CENTER));}
  }else{
   float side=16*s,top=(height-side)/2;rounded(g,1,top,side,side,3*s,checked?palette.accent:palette.surface);if(!checked)outline(g,1,top,side,side,3*s,palette.secondary,s);
   if(checked){Gdiplus::Pen p(color(RGB(255,255,255)),1.8f*s);g.DrawLine(&p,4*s,top+8*s,7*s,top+11*s);g.DrawLine(&p,7*s,top+11*s,13*s,top+4*s);}RECT bounds=r;bounds.left+=int(24*s);text(dc,h,bounds,label(h),enabled?palette.text:palette.disabled,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
  }
 }else if(cls==L"ComboBox"){
  rounded(g,1,1,w-2,height-2,6*s,hot&&enabled?palette.hover:palette.surface);if(!palette.dark)outline(g,1,1,w-2,height-2,6*s,palette.divider,.6f*s);
  RECT bounds=r;bounds.left+=int(10*s);bounds.right-=int(28*s);text(dc,h,bounds,label(h),enabled?palette.text:palette.disabled,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
  Gdiplus::Pen p(color(enabled?palette.text:palette.disabled),1.6f*s);float x=w-14*s,y=height/2;g.DrawLine(&p,x-3*s,y-2*s,x,y-5*s);g.DrawLine(&p,x,y-5*s,x+3*s,y-2*s);g.DrawLine(&p,x-3*s,y+2*s,x,y+5*s);g.DrawLine(&p,x,y+5*s,x+3*s,y+2*s);
 }else if(cls==TRACKBAR_CLASSW){
  RECT thumb{};SendMessageW(h,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));float cx=(thumb.left+thumb.right)/2.f,cy=height/2,margin=9*s;float end=std::max(margin,w-margin);cx=std::clamp(cx,margin,end);rounded(g,margin,cy-2*s,end-margin,4*s,2*s,palette.divider);rounded(g,margin,cy-2*s,std::max(0.f,cx-margin),4*s,2*s,enabled?palette.accent:palette.secondary);Gdiplus::SolidBrush shadow(color(palette.dark?RGB(25,25,25):RGB(201,201,201)));g.FillEllipse(&shadow,cx-8*s,cy-7*s,16*s,16*s);Gdiplus::SolidBrush fill(color(enabled?palette.thumb:palette.surface));g.FillEllipse(&fill,cx-8*s,cy-8*s,16*s,16*s);
  if(focus)outline(g,cx-10*s,cy-10*s,20*s,20*s,10*s,palette.accent,1.5f*s);return true;
 }else if(cls==PROGRESS_CLASSW){auto pos=SendMessageW(h,PBM_GETPOS,0,0);rounded(g,0,height/2-3*s,w,6*s,3*s,palette.divider);rounded(g,0,height/2-3*s,w*std::clamp(double(pos)/100,0.,1.),6*s,3*s,palette.accent);}
 else if(cls==L"Static"){
  auto type=style&SS_TYPEMASK;if(type==SS_OWNERDRAW)return false;
  int divider=int(reinterpret_cast<INT_PTR>(GetPropW(h,L"SanAudisco.Divider")));if(divider||type==SS_ETCHEDHORZ||type==SS_ETCHEDVERT){Gdiplus::Pen p(color(palette.divider),s);if(divider==1||type==SS_ETCHEDHORZ)g.DrawLine(&p,0.f,height/2,w,height/2);else g.DrawLine(&p,w/2,0.f,w/2,height);}
  else if(type==SS_ICON){auto icon=reinterpret_cast<HICON>(SendMessageW(h,STM_GETICON,0,0));if(icon)DrawIconEx(dc,0,0,icon,r.right,r.bottom,0,nullptr,DI_NORMAL);}
  else{UINT flags=DT_WORDBREAK;if(type==SS_CENTER)flags|=DT_CENTER;else if(type==SS_RIGHT)flags|=DT_RIGHT;if(style&SS_ENDELLIPSIS)flags=(flags&~DT_WORDBREAK)|DT_SINGLELINE|DT_END_ELLIPSIS;text(dc,h,r,label(h),static_ink(h),flags);}
 }else return false;
 if(focus)outline(g,1.5f*s,1.5f*s,w-3*s,height-3*s,7*s,palette.accent,1.5f*s);return true;
}
inline LRESULT CALLBACK subclass(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
 if(!GetPropW(h,L"SanAudisco.NativeEdit")&&(!palette.highContrast||GetPropW(h,L"SanAudisco.Divider"))&&(msg==WM_PAINT||msg==WM_PRINTCLIENT||msg==WM_PRINT)){
  if(msg!=WM_PAINT){if(paint(h,reinterpret_cast<HDC>(wp)))return 0;}
  else{PAINTSTRUCT ps{};auto dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);auto mem=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));auto old=SelectObject(mem,bitmap);bool done=paint(h,mem);if(done)BitBlt(dc,0,0,r.right,r.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bitmap);DeleteDC(mem);EndPaint(h,&ps);if(done)return 0;}
 }
 if(msg==WM_MOUSEMOVE&&!GetPropW(h,L"SanAudisco.Hover")){SetPropW(h,L"SanAudisco.Hover",reinterpret_cast<HANDLE>(1));TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);InvalidateRect(h,nullptr,FALSE);}
 if(msg==WM_MOUSELEAVE){RemovePropW(h,L"SanAudisco.Hover");InvalidateRect(h,nullptr,FALSE);}
 if(msg==WM_SIZE&&GetPropW(h,L"SanAudisco.NativeEdit")&&!GetPropW(h,L"SanAudisco.RegionUpdating")){SetPropW(h,L"SanAudisco.RegionUpdating",reinterpret_cast<HANDLE>(1));RECT r{};GetWindowRect(h,&r);SetWindowRgn(h,palette.highContrast?nullptr:CreateRoundRectRgn(0,0,r.right-r.left+1,r.bottom-r.top+1,int(12*scale(h)),int(12*scale(h))),TRUE);RemovePropW(h,L"SanAudisco.RegionUpdating");}
 if(msg==WM_NCDESTROY){RemovePropW(h,L"SanAudisco.NativeEdit");RemovePropW(h,L"SanAudisco.Hover");RemovePropW(h,L"SanAudisco.Dpi");RemovePropW(h,L"SanAudisco.Divider");RemoveWindowSubclass(h,subclass,9);}
 auto result=DefSubclassProc(h,msg,wp,lp);
 if(msg==WM_SETFOCUS||msg==WM_KILLFOCUS||msg==WM_ENABLE||msg==BM_SETCHECK||msg==BM_SETSTATE||msg==CB_SETCURSEL||msg==WM_UPDATEUISTATE)InvalidateRect(h,nullptr,FALSE);
 return result;
}
inline void attach(HWND h,int dpi){SetPropW(h,L"SanAudisco.Dpi",reinterpret_cast<HANDLE>(INT_PTR(dpi)));wchar_t cls[64]{};GetClassNameW(h,cls,64);auto style=GetWindowLongPtrW(h,GWL_STYLE);if(std::wstring(cls)==L"Edit"){SetPropW(h,L"SanAudisco.NativeEdit",reinterpret_cast<HANDLE>(1));if(!palette.highContrast)SetWindowLongPtrW(h,GWL_STYLE,style&~WS_BORDER);SendMessageW(h,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(MulDiv(9,dpi,96),MulDiv(9,dpi,96)));}if(std::wstring(cls)==L"Static"&&(style&SS_TYPEMASK)==SS_OWNERDRAW)return;SetWindowSubclass(h,subclass,9,0);}
inline void retone(HWND h){if(GetPropW(h,L"SanAudisco.NativeEdit")){auto style=GetWindowLongPtrW(h,GWL_STYLE);SetWindowLongPtrW(h,GWL_STYLE,palette.highContrast?style|WS_BORDER:style&~WS_BORDER);SetWindowPos(h,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);SendMessageW(h,WM_SIZE,0,0);}InvalidateRect(h,nullptr,TRUE);}
inline void frame(HWND h){BOOL dark=palette.dark&&!palette.highContrast;DwmSetWindowAttribute(h,20,&dark,sizeof(dark));}
inline bool draw_combo_item(const DRAWITEMSTRUCT& item){if(item.CtlType!=ODT_COMBOBOX)return false;auto selected=(item.itemState&ODS_SELECTED)!=0;auto ink=selected?(palette.highContrast?GetSysColor(COLOR_HIGHLIGHTTEXT):RGB(255,255,255)):palette.text;auto brush=CreateSolidBrush(selected?palette.accent:palette.surface);FillRect(item.hDC,&item.rcItem,brush);DeleteObject(brush);if(item.itemID!=UINT(-1)){auto n=SendMessageW(item.hwndItem,CB_GETLBTEXTLEN,item.itemID,0);if(n>=0){std::wstring value(n+1,L'\0');SendMessageW(item.hwndItem,CB_GETLBTEXT,item.itemID,reinterpret_cast<LPARAM>(value.data()));value.resize(n);auto r=item.rcItem;r.left+=int(10*scale(item.hwndItem));text(item.hDC,item.hwndItem,r,value,ink,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);}}return true;}
}
