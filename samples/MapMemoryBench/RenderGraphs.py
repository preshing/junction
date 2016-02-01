#!/usr/bin/env python
import os
import cairo
import math
import glob


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

    def mapAxisValue(self, x):
        """ Maps x to a point along the axis.
        x should already have been filtered through self.toAxis(), especially if logarithmic. """
        return (x - self.min) / (self.max - self.min) * self.size
    
    def iterLabels(self):
        """ Helper to iterate through all the tick marks along the axis. """
        lo = int(math.floor(self.min / self.step + 1 - 1e-9))
        hi = int(math.floor(self.max / self.step + 1e-9))
        for i in xrange(lo, hi + 1):
            value = i * self.step
            if self.min == 0 and i == 0:
                continue
            yield self.mapAxisValue(value), self.labeler(self.fromAxis(value))


#---------------------------------------------------
#  Graph
#---------------------------------------------------
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

    def addCurve(self, curve):
        self.curves.append(curve)

    def renderTo(self, fileName):
        xAttribs = self.xAttribs
        yAttribs = self.yAttribs

        # Create the image surface and cairo context
        surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, 140 + int(xAttribs.size + 0.5), 65 + int(yAttribs.size + 0.5))
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
            cr.move_to(0.5, 0)
            cr.rel_line_to(0, -yAttribs.size - 0.5)
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

        # Draw curves
        for curve in self.curves:
            points = curve.points
            width = 2.5
            color = curve.color
            with Saved(cr):
                cr.set_line_width(width)
                cr.set_source_rgba(*color)
                with Saved(cr):
                    cr.rectangle(0, 5, xAttribs.size, -yAttribs.size - 15)
                    cr.clip()
                    cr.move_to(xAttribs.mapAxisValue(points[0][0]), -yAttribs.mapAxisValue(points[0][1]))
                    for x, y, yHi in points[1:]:
                        cr.line_to(xAttribs.mapAxisValue(x) + 0.5, -yAttribs.mapAxisValue(y) - 0.5)
                    cr.stroke()

                # Label
                labelFont = createScaledFont('Arial', 11)
                label = curve.name
                x, y, yHi = points[-1]
                fillAlignedText(cr, xAttribs.mapAxisValue(x) + 3, -yAttribs.mapAxisValue(y) + 4, labelFont, label, 0)

        # Draw axis names
        cr.set_source_rgb(0, 0, 0)
        axisFont = createScaledFont('Helvetica', 14, weight=cairo.FONT_WEIGHT_BOLD)
        with Saved(cr):
            cr.translate(-47, -yAttribs.size / 2.0)
            cr.rotate(-math.pi / 2)
            fillAlignedText(cr, 0, 0, axisFont, "Bytes In Use", 0.5)
        fillAlignedText(cr, xAttribs.size / 2.0, 50, axisFont, "Population", 0.5)

        # Save PNG file
        surface.write_to_png(fileName)


#---------------------------------------------------
#  main
#---------------------------------------------------
graph = Graph(AxisAttribs(600, 0, 1000000, 200000), AxisAttribs(320, 0, 50000000, 10000000))    
COLORS = [
    (1, 0, 0),
    (1, 0.5, 0),
    (0.5, 0.5, 0),
    (0, 1, 0),
    (0, 0.5, 1),
    (0, 0, 1),
    (1, 0, 1)
]
for i, fn in enumerate(glob.glob('build*/results.txt')):
    points = eval(open(fn, 'r').read())
    graph.addCurve(Curve(os.path.split(fn)[0], points, COLORS[i % len(COLORS)]))
graph.renderTo('out.png')
