import numpy as np
import csv
import os
import subprocess
from threading import Thread
import signal
from bokeh.io import curdoc
from bokeh.layouts import layout,row, widgetbox, column
from bokeh.models import ColumnDataSource, Div, CustomJS, PreText
from bokeh.models.widgets import Slider, TextInput, Tabs, Panel, Button, CheckboxButtonGroup, RangeSlider, RadioButtonGroup
from bokeh.plotting import figure
from itertools import zip_longest

## Ajustando constantes importantes
OUT_FILE = "events.csv"
EVENTS_DIR = "events"
COMMAND = "../simulate"
COMMAND_MASK = "../mask_generator"
MASK_FILE = "mask.mask"
CHARGE_HISTO_FILE = "charge.histo"
SIZE_HISTO_FILE = "size.histo"

#############################
###    DESCRIPTION TAB    ###
#############################
description = Div(text=open(os.path.join(os.path.dirname(__file__),'description.html')).read(), width=800)

### Download data 
## TODO: Implementar!
#download_button = Button(label="Download data", button_type="success")
#download_button.callback = CustomJS(args=dict(source=source),
#                                   code=open(os.path.join(os.path.dirname(__file__),'download_events.html')).read())

#########################
##     MEASURE TAB    ###
#########################
button_begin_measure = RadioButtonGroup(labels=["STOP", "BEGIN"], active=0)
slider_gain = Slider(start=1, end=1023, value=1, step=1, title="Gain")
slider_evsize = Slider(start=1, end=100, value=2, step=1, title="Minimum size")
slider_trigger = RangeSlider(start=0, end=1023, value=(150,200), step=1, title="Detection threshold/trigger")

def execute(cmd):
    popen = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, universal_newlines=True)
    for stdout_line in iter(popen.stdout.readline, ""):
        yield stdout_line
        if (button_begin_measure.active == 0):
            popen.kill()
    popen.stdout.close()
    return_code = popen.wait()
    if return_code:
        raise subprocess.CalledProcessError(return_code, cmd)

def cmdtofile(cmd, filename):
    try: 
        fileout = open(os.path.join(os.path.dirname(__file__),filename),"w")
        for path in execute(cmd):          # time, size, nsatPixels, charge, coord_x, coord_y
            print(path, end="")
            fileout.write(path)
            fileout.flush()
        fileout.close()
    except KeyboardInterrupt:
        print("Proceso Terminado: Keyboard Interrupt")
        fileout.close()
    except:
        print("Proceso Terminado")
        fileout.close()

def begin_measure():
    if button_begin_measure.active == 1:
        try:
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
            thread = Thread(target=cmdtofile, args=(cmd,OUT_FILE))
            thread.start()
        except:
            pass

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
            cmd = os.path.join(os.path.dirname(__file__),COMMAND_MASK)
            cmd += " -t "+str(int(slider_timeout.value))
            cmd += " -g "+str(int(slider_gain.value))
            cmd += " -th "+str(int(slider_trigger.value[0]))
            cmd += " -n "+str(int(slider_min_number.value))
            print("Executing: "+cmd)
            thread = Thread(target=cmdtofile, args=(cmd,MASK_FILE))
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

p_mask = figure(title="Pixels in the mask",plot_width=300, plot_height=300)
source_mask = ColumnDataSource(data=dict(x=[], y=[], n=[], value=[]))
p_mask.rect('x','y', source=source_mask, alpha=0.5, width=2, height=2)
def update_mask():
    global source_mask
    if os.path.getsize(os.path.join(os.path.dirname(__file__),MASK_FILE)) != 0:
        with open(os.path.join(os.path.dirname(__file__),MASK_FILE),"r") as csv_file:
            data =  np.genfromtxt(csv_file, delimiter='\t')
        source_mask.data = dict(x=data[:,0], y=data[:,1], n=data[:,2], value=data[:,3])

########################
###    HISTO_PLOT    ###
########################

### CHARGE
p_charge_histo = figure(title="Charge histogram of events", y_axis_type="log", y_range=(0.5, 10), tools="save,xpan,xwheel_zoom", active_scroll="xwheel_zoom")
source_charge_histogram = ColumnDataSource(data=dict(x=[], y=[]))
p_charge_histo.step('x','y',source=source_charge_histogram, mode="center")

def update_charge_source():
    global source_charge_histogram
    if os.path.getsize(os.path.join(os.path.dirname(__file__),CHARGE_HISTO_FILE)) != 0:
        with open(os.path.join(os.path.dirname(__file__),CHARGE_HISTO_FILE),"r") as csv_file:
            data =  np.genfromtxt(csv_file, delimiter='\t')
        p_charge_histo.y_range.end = max(data[:,1])*1.5
        data[:,1][data[:,1] == 0] = 0.1
        source_charge_histogram.data = dict(x=data[:,0], y=data[:,1])

### SIZE
p_size_histo = figure(title="Histogram of sizes of events", y_axis_type="log", y_range=(0.5, 10), tools="save,xpan,xwheel_zoom", active_scroll="xwheel_zoom")
source_size_histogram = ColumnDataSource(data=dict(x=[], y=[]))
p_size_histo.step('x','y',source=source_size_histogram, mode="center")

def update_size_source():
    global source_size_histogram
    if os.path.getsize(os.path.join(os.path.dirname(__file__),SIZE_HISTO_FILE)) != 0:
        with open(os.path.join(os.path.dirname(__file__),SIZE_HISTO_FILE),"r") as csv_file:
            data =  np.genfromtxt(csv_file, delimiter='\t')
        p_size_histo.y_range.end = max(data[:,1])*1.5
        data[:,1][data[:,1] == 0] = 0.1
        source_size_histogram.data = dict(x=data[:,0], y=data[:,1])

histo_tab = Tabs(tabs=[ Panel(child=p_charge_histo, title="Charge"),
                        Panel(child=p_size_histo, title="Size")], width=650)

#######################
###    EVENT_PLOT   ###
#######################
def update_event(attrname, old, new):
    with open(os.path.join(os.path.dirname(__file__),"events",new+".pgm"),"r") as csv_file:
        lines = csv_file.readlines()
    meta_data = np.fromstring(lines[4][1:], dtype=float, sep='\t')
    #Time	Size	Charge	Origin_x	Origin_y	Center_x	Center_y	Threshold
    largo = len(np.fromstring(lines[5][:], dtype=float, sep='\t'))
    alto = len(np.fromstring(lines[5:][0], dtype=float, sep='\t'))
    p_event.x_range.start = meta_data[3]; p_event.x_range.end = meta_data[3]+largo
    p_event.y_range.start = meta_data[4]; p_event.y_range.end = meta_data[4]+alto
    source_image_event.data = dict(x=[meta_data[3]],y=[meta_data[4]],dw=[largo],dh=[alto],
        image=[np.genfromtxt(lines[5:], delimiter='\t').astype(int)], size=[meta_data[1]], charge=[meta_data[2]])

p_event = figure(title="Evento", x_range=(0,1), y_range=(0,1), tools="save,hover", active_drag=None,
    tooltips=[("x", "$x{0}"), ("y", "$y{0}"), ("ADC Value", "@image"), ("Charge", "@charge"), ("Size", "@size")])
p_event.sizing_mode = 'scale_height'
source_image_event = ColumnDataSource(data=dict(x=[],y=[],dw=[],dh=[], image=[], charge=[], size=[]))
p_event.image(image='image', source=source_image_event, x='x', y='y', dw='dw', dh='dh', palette="Greys256")
text_event = TextInput(value="[Insert index of event]", title="Event:")
text_event.on_change('value', update_event)

####################
###    UPDATE    ###
####################
update_button = Button(label="Update", button_type="primary")
text_event_number = PreText(text='')

def text_event_update():
    text_event_number.text = "Found "+str(sum(source_size_histogram.data['y'].astype(int)))+" events\nThe mask has "+str(len(source_mask.data['x']))+" pixels"

def update():
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
            [description, column(Div(text="""<br /><br />"""),widgetbox(update_button),text_event_number)],
            [column(measure_tab, p_mask), histo_tab , column( widgetbox(text_event) ,p_event )]
        ],
        sizing_mode='fixed',
    )

curdoc().add_root( lay )
curdoc().title = "Radmonpi++"