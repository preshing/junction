#!/usr/bin/env python
import os
import cairo
import math
import glob
import collections


def colorTuple(h):
    return (int(h[0:2], 16) / 255.0, int(h[2:4], 16) / 255.0, int(h[4:6], 16) / 255.0)

ALL_MAPS = [
    ('folly',           colorTuple('606080')),
    ('tervel',          colorTuple('408040')),
    ('stdmap',          colorTuple('b0b090')),
    ('nbds',            colorTuple('9090b0')),
    ('linear',          colorTuple('ff4040')),
    ('michael',         colorTuple('202020')),
    ('tbb',             colorTuple('0090b0')),
    ('cuckoo',          colorTuple('d040d0')),
    ('grampa',          colorTuple('ff6040')),
    ('leapfrog',        colorTuple('ff8040')),
]

#---------------------------------------------------
#  Cairo drawing helpers
#---------------------------------------------------
def createScaledFont(family, size, slant=cairo.FONT_SLANT_NORMAL, weight=cairo.FONT_WEIGHT_NORMAL):
    """ Simple helper function to create a cairo ScaledFont. """
    face = cairo.ToyFontFace(family, slant, weight)
    DEFAULT_FONT_OPTIONS = cairo.FontOptions()
    DEFAULT_FONT_OPTIONS.set_antialias(cairo.ANTIALIAS_SUBPIXEL)
    return cairo.ScaledFont(face, cairo.Matrix(xx=size, yy=size), cairo.Matrix(), DEFAULT_FONT_OPTIONS)

def fillAlignedText(cr, x, y, scaledFont, text, alignment = 0):
    """ Draw some aligned text at the specified co-ordinates.
    alignment = 0: left-justify
    alignment = 0.5: center
    alignment = 1: right-justify """
    ascent, descent = scaledFont.extents()[:2]
    x_bearing, y_bearing, width, height = scaledFont.text_extents(text)[:4]
    with Saved(cr):
        cr.set_scaled_font(scaledFont)
        cr.move_to(math.floor(x + 0.5 - width * alignment), math.floor(y + 0.5))
        cr.text_path(text)
        cr.fill()

class Saved():
    """ Preserve cairo state inside the scope of a with statement. """
    def __init__(self, cr):
        self.cr = cr
    def __enter__(self):
        self.cr.save()
        return self.cr
    def __exit__(self, type, value, traceback):
        self.cr.restore()


#---------------------------------------------------
#  AxisAttribs
#---------------------------------------------------
class AxisAttribs:
    """ Describes one axis on the graph. Can be linear or logarithmic. """
    
    def __init__(self, size, min, max, step, logarithmic = False, labeler = lambda x: str(int(x + 0.5))):
        self.size = float(size)
        self.logarithmic = logarithmic
        self.labeler = labeler
        self.toAxis = lambda x: math.log(x) if logarithmic else float(x)
        self.fromAxis = lambda x: math.exp(x) if logarithmic else float(x)
        self.min = self.toAxis(min)
        self.max = self.toAxis(max)
        self.step = self.toAxis(step)

    def setMinMax(self, min, max):
        self.min = self.toAxis(min)
        self.max = self.toAxis(max)

    def mapAxisValue(self, x):
        """ Maps x to a point along the axis. """
        return (self.toAxis(x) - self.min) / (self.max - self.min) * self.size
    
    def iterLabels(self):
        """ Helper to iterate through all the tick marks along the axis. """
        lo = int(math.floor(self.min / self.step + 1 - 1e-9))
        hi = int(math.floor(self.max / self.step + 1e-9))
        for i in xrange(lo, hi + 1):
            value = i * self.step
            if self.min == 0 and i == 0:
                continue
            yield self.mapAxisValue(self.fromAxis(value)), self.labeler(self.fromAxis(value))


#---------------------------------------------------
#  Graph
#---------------------------------------------------
def makeNamedTuples(results, typeName = 'Point', labels = 'labels', points = 'points'):
    namedTupleType = collections.namedtuple(typeName, results[labels])
    numLabels = len(results[labels])
    return [namedTupleType(*p[:numLabels]) for p in results[points]]

class Curve:
    def __init__(self, name, points, color):
        self.name = name
        self.points = points
        self.color = color
        
class Graph:
    """ Renders a graph. """
    
    def __init__(self, xAttribs, yAttribs):
        self.xAttribs = xAttribs
        self.yAttribs = yAttribs
        self.curves = []
        self.xMin = None
        self.xMax = None

    def addCurve(self, curve):
        self.curves.append(curve)
        xMin = min(x for x, y in curve.points)
        if self.xMin is None or xMin < self.xMin:
            self.xMin = xMin
        xMax = max(x for x, y in curve.points)
        if self.xMax is None or xMax > self.xMax:
            self.xMax = xMax

    def renderTo(self, fileName):
        xAttribs = self.xAttribs
        yAttribs = self.yAttribs
        xAttribs.setMinMax(self.xMin, self.xMax)

        # Create the image surface and cairo context
        surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, 330 + int(xAttribs.size + 0.5), 55 + int(yAttribs.size + 0.5))
        cr = cairo.Context(surface)
        cr.set_source_rgb(1, 1, 1)
        cr.paint()
        cr.set_miter_limit(1.414)
        cr.translate(80, 8 + yAttribs.size)

        # Draw axes
        labelFont = createScaledFont('Arial', 13)
        with Saved(cr):
            cr.set_line_width(1)
            cr.set_source_rgb(.4, .4, .4)

            # Horizontal axis
            cr.move_to(0, -0.5)
            cr.rel_line_to(xAttribs.size + 1, 0)
            cr.stroke()
            for pos, label in xAttribs.iterLabels():    # Labels
                x = math.floor(pos + 0.5)
                with Saved(cr):
                    cr.translate(x, 9)
                    fillAlignedText(cr, 0, 6, labelFont, label, 0.5)

            # Vertical axis
            cr.set_source_rgb(*colorTuple('f0f0f0'))                
            for pos, label in yAttribs.iterLabels():    # Background lines
                if label == '0':
                    continue
                y = -math.floor(pos + 0.5) - 0.5
                cr.move_to(1, y)
                cr.rel_line_to(xAttribs.size + 1, 0)
            cr.stroke()
            cr.set_source_rgb(.4, .4, .4)
            cr.move_to(0.5, 0)
            cr.rel_line_to(0, -yAttribs.size - 0.5)
            if False:
                for pos, label in yAttribs.iterLabels():    # Tick marks
                    if label == '0':
                        continue
                    y = -math.floor(pos + 0.5) - 0.5
                    cr.move_to(1, y)
                    cr.rel_line_to(-4, 0)
            cr.stroke()
            for pos, label in yAttribs.iterLabels():    # Labels
                if label == '0':
                    continue
                fillAlignedText(cr, -4, -pos + 4, labelFont, label, 1)

        """
        with Saved(cr):
            x = xAttribs.size - 70.5 + 80
            y = -234.5
            cr.rectangle(x, y, 120, 82)
            cr.set_source_rgb(*colorTuple('ffffff'))                
            cr.fill()
            cr.set_source_rgb(*colorTuple('f0f0f0'))                
            cr.rectangle(x, y, 120, 82)
            cr.set_line_width(1)
            cr.stroke()
        """

        # Draw curves
        for cn, curve in enumerate(self.curves):
            points = curve.points
            color = curve.color
            width = 1.75
            #if color == colorTuple('ff4040'):
                #width = 2
            with Saved(cr):
                cr.set_line_width(width)
                cr.set_source_rgba(*color)
                #if color == colorTuple('9090b0'):
                #    cr.set_dash([9, 2])
                with Saved(cr):
                    cr.rectangle(0, 5, xAttribs.size, -yAttribs.size - 15)
                    cr.clip()
                    x, y = points[0]
                    cr.move_to(xAttribs.mapAxisValue(x), -yAttribs.mapAxisValue(y))
                    for x, y in points[1:]:
                        cr.line_to(xAttribs.mapAxisValue(x) + 0.5, -yAttribs.mapAxisValue(y) - 0.5)
                    cr.stroke()
                for x, y in points:
                    cr.rectangle(xAttribs.mapAxisValue(x) - 2.5, -yAttribs.mapAxisValue(y) - 2.5, 5, 5)
                cr.fill()

                x = xAttribs.size + 40
                y = -120 + (5 - cn) * 14
                cr.move_to(x - 4.5, y - 4.5)
                cr.rel_line_to(-21, 0)
                cr.stroke()

                # Label
                weight = cairo.FONT_WEIGHT_NORMAL
                if color == colorTuple('ff4040'):
                    weight = cairo.FONT_WEIGHT_BOLD
                labelFont = createScaledFont('Arial', 13, weight=weight)
                label = curve.name
                #x, y = points[-1]
                #fillAlignedText(cr, xAttribs.mapAxisValue(x) + 3, -yAttribs.mapAxisValue(y) + 4, labelFont, label, 0)
                x = xAttribs.size + 40
                y = -120 + (5 - cn) * 14
                fillAlignedText(cr, x, y, labelFont, label, 0)

        # Draw axis names
        cr.set_source_rgb(0, 0, 0)
        axisFont = createScaledFont('Helvetica', 16, weight=cairo.FONT_WEIGHT_BOLD)
        with Saved(cr):
            cr.translate(-66, -yAttribs.size / 2.0)
            cr.rotate(-math.pi / 2)
            fillAlignedText(cr, 0, 0, axisFont, 'Map Operations / Sec', 0.5)
        with Saved(cr):
            axisFont2 = createScaledFont('Helvetica', 13)
            cr.translate(-50, -yAttribs.size / 2.0)
            cr.rotate(-math.pi / 2)
            cr.set_source_rgba(*colorTuple('808080'))
            fillAlignedText(cr, 0, 0, axisFont2, '(Total Across All Threads)', 0.5)
        fillAlignedText(cr, xAttribs.size / 2.0, 42, axisFont, 'Threads', 0.5)

        # Save PNG file
        surface.write_to_png(fileName)


#---------------------------------------------------
#  main
#---------------------------------------------------
def formatTime(x):
    if x < 0.9e-6:
        return '%d ns' % (x * 1e9 + 0.5)
    elif x < 0.9e-3:
        return '%d us' % (x * 1e6 + 0.5)  # FIXME: Micro symbol
    if x < 0.9:
        return '%d ms' % (x * 1e3 + 0.5)
    else:
        return '%d s' % (x + 0.5)

graph = Graph(AxisAttribs(250, 1, 6, 1),
              AxisAttribs(240, 0, 150, 10, False, lambda x: '%dM' % x if x % 50 == 0 else ''))

for suffix, color in ALL_MAPS:
    resultsPath = 'build-%s/results.txt' % suffix
    if os.path.exists(resultsPath):
        with open(resultsPath, 'r') as f:
            results = eval(f.read())
            dataPoints = makeNamedTuples(results)
            def makeGraphPoint(pt):
                mapOpsPerSec = pt.mapOpsDone / pt.totalTime
                return (pt.numThreads, mapOpsPerSec * pt.numThreads / 1000000)
            graphPoints = [makeGraphPoint(pt) for pt in dataPoints]
            graph.addCurve(Curve(results['mapType'], graphPoints, color))

graph.renderTo('out.png')
