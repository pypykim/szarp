#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# vim: set fileencoding=utf-8 :

import sys

from optparse import OptionParser , OptionGroup 

from libipk.params import Params
from libipk.plugins import Plugins

DEFAULT_PLUGINS = 'plugins/'

class App :
	def __init__( self ) :
		self.usage = 'usage: $prog [options] <Plugin>'

		self.plugins = Plugins()
		self.params = None

		self.parse_cmdline()


	def parse_cmdline( self ) :
		self.parser = OptionParser( usage = self.usage )
		self.parser.add_option("-L" , "--list-plugins" ,
				dest="list_plugins" ,
				action="store_true" ,
				help = "shows list of available plugins and quits" )
		self.parser.add_option("-H" , "--help-plugin" ,
				dest="help_plugin"  ,
				action="store_true" ,
				help = "shows help for specified plugin and quits" )
		self.parser.add_option("-p" , "--params" ,
				metavar = "<params.xml>" ,
				help = "xml file with configuration" )
		self.parser.add_option("-b" , "--base" ,
				metavar="<prefix>" ,
				help = "SZARP base prefix" )
		self.parser.add_option("-x" , "--xpath" ,
				metavar="<xpath>" ,
				help = "xpath on witch plugin should operate" )
		self.parser.add_option("-r" , "--read-only" ,
				action="store_true" ,
				help = "turn read-only mode (not implemented yet)" )
		self.parser.add_option("" , "--load" ,
				metavar = "<path>" ,
				help = "directory where plugins are stored. Defaults to %s." % DEFAULT_PLUGINS ,
				default = DEFAULT_PLUGINS )

		self.options , self.args = self.parser.parse_args()

	def print_plugin_list( self ) :
		if self.options.list_plugins :
			print("Available plugins:")
			print("")
			for n in self.plugins.names() :
				print("  %s    \t%s" % ( n , self.plugins.help(n).splitlines()[0] ) )
			sys.exit(0)

	def print_plugin_help( self ) :
		if self.options.help_plugin :
			print( 'Help for %s:' % self.plgname )
			print()
			for h in self.plugins.help(self.plgname).splitlines() :
				print("  %s" % h )
			sys.exit(0)

	def validate_options( self ) :
		if self.options.params and self.options.base :
			self.parser.error("Options --params and --base are mutually exclusive.")
		if not self.options.params and not self.options.base :
			self.parser.error("One of --params or --base must be specified.")

	def validate_args( self ) :

		if len(self.args) < 1 :
			self.parser.error("Plugin must be specified.")

		if len(self.args) > 1 :
			self.parser.error("Unknown options specified.")

		if not self.plugins.available( self.args[0] ) :
			self.parser.error("No such plugin available. Use -L to list available plugins.")

		self.plgname = self.args[0]

	def ask_for_args( self , args ) :
		res = {}
		for a in args :
			if sys.version_info < (3,0) :
				res[a] = raw_input(a + ": ")
			else :
				res[a] = input(a + ":  ")
		return res

	def run( self ) :
		self.plugins.load(self.options.load)

		self.print_plugin_list()

		self.validate_args()

		self.print_plugin_help()

		self.validate_options()

		plg = self.plugins.get( self.plgname )

		args = self.ask_for_args( plg.get_args() )

		xpath = self.options.xpath if self.options.xpath != None else plg.xpath

		if self.options.params : 
			paramsfile = self.options.params 
		else :
			# TODO: use custom sharp install path
			paramsfile = "/opt/szarp/" + self.options.base + "/config/params.xml"

		self.params = Params(paramsfile)

		self.params.apply( xpath , plg.process , args )
		
		print( plg.result_pretty() )

		self.params.close()

		return 0

if __name__ == '__main__' :

	app = App()
	res = app.run()
	sys.exit(res)

