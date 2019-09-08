import numpy as np
import pandas as pd
import csv
import os
import subprocess
from threading import Thread
import signal
from bokeh.io import curdoc, export_svgs
from bokeh.layouts import layout,row, widgetbox, column
from bokeh.models import ColumnDataSource, Div, CustomJS, PreText, LinearColorMapper
from bokeh.models.widgets import Slider, TextInput, Tabs, Panel, Button, CheckboxButtonGroup, RangeSlider, RadioButtonGroup
from bokeh.plotting import figure
from itertools import zip_longest

## Ajustando constantes importantes
COMMAND = "../radmonpi"
COMMAND_MASK = "../mask_generator"

OUT_FILE = "static/events.csv"
MASK_FILE = "static/mask.mask"
CHARGE_HISTO_FILE = "static/charge.histo"
SIZE_HISTO_FILE = "static/size.histo"
MASK_HISTO_FILE = "static/mask_photo.histo"
EVENTS_DIR = "static/events"

#############################
###    DESCRIPTION TAB    ###
#############################
description = Div(text=open(os.path.join(os.path.dirname(__file__),'description.html')).read(), width=700)

### Download data 
## TODO: Implementar!
download_button = Button(label="Download data", button_type="success")
download_button.callback = CustomJS(code=open(os.path.join(os.path.dirname(__file__),'download_events.js')).read())

download_button_wo = Button(label="Download data (without images)", button_type="success")
download_button_wo.callback = CustomJS(code=open(os.path.join(os.path.dirname(__file__),'download_wo_events.js')).read())

#########################
##     MEASURE TAB    ###
#########################
button_begin_measure = RadioButtonGroup(labels=["STOP", "BEGIN"], active=0)
slider_gain = Slider(start=1, end=1023, value=1, step=1, title="Gain")
slider_evsize = Slider(start=1, end=100, value=2, step=1, title="Minimum size")
slider_trigger = RangeSlider(start=0, end=1023, value=(150,200), step=1, title="Detection threshold/trigger")

def run(cmd, filename, endbutton):
    try:
        logfile = open(os.path.join(os.path.dirname(__file__),filename),"w")
        p = subprocess.Popen(cmd.split(), universal_newlines=True, stdout=logfile)
        ret_code = p.poll()
        while ret_code is None:
            if endbutton[0].active == 0:
                p.terminate()
            ret_code = p.poll()
        logfile.flush()
        logfile.close()
        endbutton[0].active = 0
        print("Proceso Terminado")
        return ret_code
    except:
        endbutton[0].active = 0
        print(button_begin_mask.active)
        print("Proceso Terminado")
        pass

def begin_measure():
    if button_begin_measure.active == 1:
        try:
            button_begin_mask.active = 0
            cmd = os.path.join(os.path.dirname(__file__),COMMAND)
            cmd += " -g "+str(int(slider_gain.value))
            cmd += " -eva "+str(int(slider_evsize.value))
            cmd += " -evth "+str(int(slider_trigger.value[0]))
            cmd += " -evtr "+str(int(slider_trigger.value[1]))
            cmd += " -evs "+os.path.join(os.path.dirname(__file__),EVENTS_DIR)
            cmd += " -mask "+os.path.join(os.path.dirname(__file__),MASK_FILE)
            cmd += " -ch "+os.path.join(os.path.dirname(__file__),CHARGE_HISTO_FILE)
            cmd += " -sh "+os.path.join(os.path.dirname(__file__),SIZE_HISTO_FILE) 
            print("Executing: "+cmd)
            thread = Thread(target=run, args=(cmd,OUT_FILE,[button_begin_measure]))
            thread.start()
        except:
            pass
    if button_begin_measure.active == 0:
        print("Measure ended")

button_begin_measure.on_change('active', lambda attr, old, new: begin_measure())

############################
###    MASK_GENERATOR    ###
############################
button_begin_mask = RadioButtonGroup(labels=["STOP", "BEGIN"], active=0)
slider_timeout = Slider(start=0, end=600, value=0, step=10, title="Timeout")
slider_min_number = Slider(start=1, end=100, value=2, step=1, title="Threshold for frames")

def begin_mask():
    if button_begin_mask.active == 1:
        try:
            button_begin_measure.active = 0
            cmd = os.path.join(os.path.dirname(__file__),COMMAND_MASK)
            cmd += " -t "+str(int(slider_timeout.value))
            cmd += " -g "+str(int(slider_gain.value))
            cmd += " -th "+str(int(slider_trigger.value[0]))
            cmd += " -n "+str(int(slider_min_number.value))
            cmd += " -ph "+os.path.join(os.path.dirname(__file__),MASK_HISTO_FILE)
            print("Executing: "+cmd)
            thread = Thread(target=run, args=(cmd,MASK_FILE,[button_begin_mask]))
            thread.start()
        except:
            pass

button_begin_mask.on_change('active', lambda attr, old, new: begin_mask())

measure_tab = widgetbox(button_begin_measure,slider_gain,slider_evsize,slider_trigger,
                        Div(text="""<br/><br/>mask_generator"""),
                        button_begin_mask, slider_timeout, slider_min_number)

#######################
###    MASK_PLOT    ###
#######################

p_mask = figure(title="Pixels evaluated in the mask",plot_width=300, plot_height=300)
source_mask = ColumnDataSource(data=dict(x=[], y=[], n=[], value=[]))
p_mask.rect('x','y', source=source_mask, alpha=0.5, width=2, height=2)
def update_mask():
    global source_mask
    if os.path.exists(os.path.join(os.path.dirname(__file__),MASK_FILE)):
        try:
            with open(os.path.join(os.path.dirname(__file__),MASK_FILE),"r") as csv_file:
                data = np.genfromtxt(csv_file, delimiter='\t')
            source_mask.data = dict(x=data[:,0], y=data[:,1], n=data[:,2], value=data[:,3])
        except:
            print("EXCEPTION found while updating mask.")

p_mask.output_backend = "webgl"

########################
###    HISTO_PLOT    ###
########################

### PHOTO_MASK
p_photo_histo = figure(title="Histogram of ADV pixels in the mas", y_axis_type="log", y_range=(0.5, 10), tools="save,xpan,xwheel_zoom", active_scroll="xwheel_zoom")
source_photo_histogram = ColumnDataSource(data=dict(x=[], y=[]))
p_photo_histo.step('x','y',source=source_photo_histogram, mode="center")
p_photo_histo.output_backend = "webgl"

def update_photo_source():
    global source_photo_histogram
    if os.path.exists(os.path.join(os.path.dirname(__file__),MASK_HISTO_FILE)):
        with open(os.path.join(os.path.dirname(__file__),MASK_HISTO_FILE),"r") as csv_file:
            data =  np.genfromtxt(csv_file, delimiter='\t')
        p_photo_histo.y_range.end = max(data[:,1])*1.5
        data[:,1][data[:,1] == 0] = 0.1
        source_photo_histogram.data = dict(x=data[:,0], y=data[:,1])

### CHARGE
p_charge_histo = figure(title="Charge histogram of events", y_axis_type="log", y_range=(0.5, 10), tools="save,xpan,xwheel_zoom", active_scroll="xwheel_zoom")
source_charge_histogram = ColumnDataSource(data=dict(x=[], y=[]))
p_charge_histo.step('x','y',source=source_charge_histogram, mode="center")
p_charge_histo.output_backend = "webgl"

def update_charge_source():
    global source_charge_histogram
    if os.path.exists(os.path.join(os.path.dirname(__file__),CHARGE_HISTO_FILE)):
        try:
            with open(os.path.join(os.path.dirname(__file__),CHARGE_HISTO_FILE),"r") as csv_file:
                data =  np.genfromtxt(csv_file, delimiter='\t')
            p_charge_histo.y_range.end = max(data[:,1])*1.5
            data[:,1][data[:,1] == 0] = 0.1
            source_charge_histogram.data = dict(x=data[:,0], y=data[:,1])
        except:
            print("EXCEPTION found while updating charge_histogram.")

### SIZE
p_size_histo = figure(title="Histogram of sizes of events", y_axis_type="log", y_range=(0.5, 10), tools="save,xpan,xwheel_zoom", active_scroll="xwheel_zoom")
source_size_histogram = ColumnDataSource(data=dict(x=[], y=[]))
p_size_histo.step('x','y',source=source_size_histogram, mode="center")
p_size_histo.output_backend = "webgl"

def update_size_source():
    global source_size_histogram
    if os.path.exists(os.path.join(os.path.dirname(__file__),SIZE_HISTO_FILE)):
        try:
            with open(os.path.join(os.path.dirname(__file__),SIZE_HISTO_FILE),"r") as csv_file:
                data =  np.genfromtxt(csv_file, delimiter='\t')
            p_size_histo.y_range.end = max(data[:,1])*1.5
            data[:,1][data[:,1] == 0] = 0.1
            source_size_histogram.data = dict(x=data[:,0], y=data[:,1])
        except:
            print("EXCEPTION found while updating size_histogram.")

histo_tab = Tabs(tabs=[ Panel(child=p_charge_histo, title="Charge"),
                        Panel(child=p_size_histo, title="Size"),
                        Panel(child=p_photo_histo, title="Pixels in the Mask")], width=650)

#######################
###    EVENT_PLOT   ###
#######################
def update_colors(attrname, old, new):
    p_event.select_one(LinearColorMapper).update(low=new[0], high=new[1])

slider_events_palette = RangeSlider(start=0, end=1023, value=(0,1023), step=1, title="Palette")
slider_events_palette.on_change('value', update_colors)

def update_event(attrname, old, new):
    with open(os.path.join(os.path.dirname(__file__),EVENTS_DIR,new+".pgm"),"r") as csv_file:
        lines = csv_file.readlines()
    meta_data = np.fromstring(lines[4][1:], dtype=float, sep='\t')
    #Time	Size	Charge	Origin_x	Origin_y	Center_x	Center_y	Threshold
    largo = len(np.fromstring(lines[5][:], dtype=float, sep='\t'))
    alto = len(np.fromstring(lines[5:][0], dtype=float, sep='\t'))
    p_event.x_range.start = meta_data[3]; p_event.x_range.end = meta_data[3]+largo+1
    p_event.y_range.start = meta_data[4]; p_event.y_range.end = meta_data[4]+alto+1
    source_image_event.data = dict(x=[meta_data[3]],y=[meta_data[4]],dw=[largo+1],dh=[alto+1],
        image=[np.genfromtxt(lines[5:], delimiter='\t').astype(int)], size=[meta_data[1]], charge=[meta_data[2]])
    slider_events_palette.value = ( np.amin(source_image_event.data['image']), np.amax(source_image_event.data['image']) )

p_event = figure(title="Event", x_range=(0,1), y_range=(0,1), tools="save,hover", plot_width=550, plot_height=550,
    tooltips=[("x", "$x{0}"), ("y", "$y{0}"), ("ADC Value", "@image"), ("Charge", "@charge"), ("Size", "@size")])
source_image_event = ColumnDataSource(data=dict(x=[],y=[],dw=[],dh=[], image=[], charge=[], size=[]))
p_event.image(image='image', source=source_image_event, x='x', y='y', dw='dw', dh='dh', color_mapper=LinearColorMapper(palette="Greys256",low=0,high=1023))
p_event.output_backend = "webgl"

text_event = TextInput(value="[Insert index of event]", title="Number of event:")
text_event.on_change('value', update_event)

widget_event = widgetbox(text_event, slider_events_palette, width=550)

####################
###    UPDATE    ###
####################
update_button = Button(label="Update", button_type="primary")
text_event_number = PreText(text='')

def text_event_update():
    try:
        text_event_number.text = "Found "+str(sum(source_size_histogram.data['y'].astype(int)))+" events\nThe mask has "+str(len(source_mask.data['x']))+" pixels"
    except:
        pass

def update():
    update_photo_source()
    update_size_source()
    update_charge_source()
    update_mask()
    text_event_update()

update_button.on_click(update)
update()        #First update before start

#####################
###     LAYOUT    ###
#####################
lay =layout(
        children=[
            [description, column(widgetbox(update_button),text_event_number), widgetbox(download_button,download_button_wo)],
            [column(measure_tab, p_mask), histo_tab , column( widget_event ,p_event )]
        ],
        sizing_mode='fixed',
    )

curdoc().add_root( lay )
curdoc().title = "Radmonpi++"