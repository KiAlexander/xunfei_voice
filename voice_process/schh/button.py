#!/usr/bin/python
#encoding:utf-8  
from Tkinter import *
import threading
import os

def start_button():
    t =threading.Thread(target=voice_thread)
    t.start()
    
def voice_thread():
    #os.popen('~/xunfei_voice/samples/schh/schh')
    os.system('./schh')

def stop_button():
    os.system('ps -ef|grep schh|grep -v grep|cut -c 9-15|xargs kill -s 9')

root = Tk()
btn1 = Button(root,text = '开始程序',command = start_button).pack()
btn2 = Button(root,text = '结束程序',command = stop_button).pack()

root.mainloop()
#while True:
#    root.update_idletasks()
