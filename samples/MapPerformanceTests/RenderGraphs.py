#!/usr/bin/env python
import os
import cairo
import math
import glob
import collections


def colorTuple(h):
    return (int(h[0:2], 16) / 255.0, int(h[2:4], 16) / 255.0, int(h[4:6], 16) / 255.0)

NULL_MAP = 'null'
ALL_MAPS = [
    ('folly',           colorTuple('606080')),
    ('tervel',          colorTuple('9090b0')),
    ('stdmap',          colorTuple('606080')),
    ('nbds',            colorTuple('9090b0')),
    ('michael',         colorTuple('606080')),
    ('tbb',             colorTuple('9090b0')),
    ('linear',          colorTuple('ff4040')),
    ('grampa',          colorTuple('ff4040')),
    ('leapfrog',        colorTuple('ff4040')),
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
        surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, 150 + int(xAttribs.size + 0.5), 65 + int(yAttribs.size + 0.5))
        cr = cairo.Context(surface)
        cr.set_source_rgb(1, 1, 1)
        cr.paint()
        cr.set_miter_limit(1.414)
        cr.translate(58, 11 + yAttribs.size)

        # Draw axes
        labelFont = createScaledFont('Arial', 11)
        with Saved(cr):
            cr.set_line_width(1)
            cr.set_source_rgb(.4, .4, .4)

            # Horizontal axis
            cr.move_to(0, -0.5)
            cr.rel_line_to(xAttribs.size + 1, 0)
            for pos, label in xAttribs.iterLabels():    # Tick marks
                x = math.floor(pos + 0.5) + 0.5
                cr.move_to(x, -1)
                cr.rel_line_to(0, 4)
            cr.stroke()
            for pos, label in xAttribs.iterLabels():    # Labels
                x = math.floor(pos + 0.5)
                with Saved(cr):
                    cr.translate(x - 1, 5)
                    cr.rotate(-math.pi / 4)
                    fillAlignedText(cr, 0, 6, labelFont, label, 1)

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

        with Saved(cr):
            x = xAttribs.size - 70.5
            y = -234.5
            cr.rectangle(x, y, 120, 82)
            cr.set_source_rgb(*colorTuple('ffffff'))                
            cr.fill()
            cr.set_source_rgb(*colorTuple('f0f0f0'))                
            cr.rectangle(x, y, 120, 82)
            cr.set_line_width(1)
            cr.stroke()

        # Draw curves
        for cn, curve in enumerate(self.curves):
            points = curve.points
            width = 2.5
            color = curve.color
            with Saved(cr):
                cr.set_line_width(width)
                cr.set_source_rgba(*color)
                if cn in [1, 3, 5]:
                    cr.set_dash([10, 1])
                with Saved(cr):
                    cr.rectangle(0, 5, xAttribs.size, -yAttribs.size - 15)
                    cr.clip()
                    x, y = points[0]
                    cr.move_to(xAttribs.mapAxisValue(x), -yAttribs.mapAxisValue(y))
                    for x, y in points[1:]:
                        cr.line_to(xAttribs.mapAxisValue(x) + 0.5, -yAttribs.mapAxisValue(y) - 0.5)
                    x = xAttribs.size - 40
                    y = -220 + cn * 12
                    cr.move_to(x - 4.5, y - 4.5)
                    cr.rel_line_to(-21, 0)
                    cr.stroke()

                # Label
                labelFont = createScaledFont('Arial', 11)
                label = curve.name
                #x, y = points[-1]
                #fillAlignedText(cr, xAttribs.mapAxisValue(x) + 3, -yAttribs.mapAxisValue(y) + 4, labelFont, label, 0)
                x = xAttribs.size - 40
                y = -220 + cn * 12
                fillAlignedText(cr, x, y, labelFont, label, 0)

        # Draw axis names
        cr.set_source_rgb(0, 0, 0)
        axisFont = createScaledFont('Helvetica', 16, weight=cairo.FONT_WEIGHT_BOLD)
        with Saved(cr):
            cr.translate(-44, -yAttribs.size / 2.0)
            cr.rotate(-math.pi / 2)
            fillAlignedText(cr, 0, 0, axisFont, 'CPU Time Spent in Map', 0.5)
        fillAlignedText(cr, xAttribs.size / 2.0, 50, axisFont, 'Interval Between Map Operations', 0.5)

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

graph = Graph(AxisAttribs(550, 1e-9, 10000e-9, 10, True, formatTime),
              AxisAttribs(240, 0, 1, 0.1, False, lambda x: '%d%%' % int(x * 100 + 0.5)))

nullResultFn = 'build-%s/results.txt' % NULL_MAP
nullResults = eval(open(nullResultFn, 'r').read())
nullPoints = makeNamedTuples(nullResults)

for suffix, color in ALL_MAPS:
    resultsPath = 'build-%s/results.txt' % suffix
    if os.path.exists(resultsPath):
        with open(resultsPath, 'r') as f:
            results = eval(f.read())
            dataPoints = makeNamedTuples(results)
            def makeGraphPoint(nullPt, pt):
                wuPerOp = float(nullPt.workUnitsDone) / nullPt.mapOpsDone
                timePerWU = nullPt.totalTime / nullPt.workUnitsDone
                timeBetweenOps = wuPerOp * timePerWU
                mapTime = pt.totalTime - pt.workUnitsDone * timePerWU
                return (timeBetweenOps, mapTime / pt.totalTime)
            graphPoints = [makeGraphPoint(*pair) for pair in zip(nullPoints, dataPoints)]
            def smooth(pts):
                result = []
                for i in xrange(len(pts) - 1):
                    x0, y0 = pts[i]
                    x1, y1 = pts[i + 1]
                    result += [(0.75*x0 + 0.25*x1, 0.75*y0 + 0.25*y1),
                               (0.25*x0 + 0.75*x1, 0.25*y0 + 0.75*y1)]
                return result
            def smooth2(pts):
                result = []
                for i in xrange(len(pts) - 1):
                    x0, y0 = pts[i]
                    x1, y1 = pts[i + 1]
                    result += [(0.5*x0 + 0.5*x1, 0.5*y0 + 0.5*y1)]
                return result

            graphPoints = smooth2(graphPoints)
            graphPoints = smooth2(graphPoints)
            graphPoints = smooth2(graphPoints)
            graphPoints = smooth2(graphPoints)
            graphPoints = smooth2(graphPoints)
            graphPoints = smooth2(graphPoints)
            graphPoints = smooth(graphPoints)
            graphPoints = smooth2(graphPoints)
            graph.addCurve(Curve(results['mapType'], graphPoints, color))

graph.renderTo('out.png')
