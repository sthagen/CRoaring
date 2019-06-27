#!/usr/bin/env python3
########################################################################
# Generates a new release.
########################################################################
import sys
import re
import subprocess
import io
import os


def extractnumbers(s):
    return tuple(map(int,re.findall("(\d+)\.(\d+)\.(\d+)",str(s))[0]))

def toversionstring(major, minor, rev):
    return str(major)+"."+str(minor)+"."+str(rev)

def topaddedversionstring(major, minor, rev):
    return str(major)+str(minor).zfill(3)+str(rev).zfill(3)

pipe = subprocess.Popen(["git", "rev-parse", "--abbrev-ref", "HEAD"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
branchresult = pipe.communicate()[0].decode().strip()

if(branchresult != "master"):
    print("release on master, you are on '"+branchresult+"'")
    sys.exit(-1)


ret = subprocess.call(["git", "remote", "update"])

if(ret != 0):
    sys.exit(ret)



pipe = subprocess.Popen(["git", "log", "HEAD..", "--oneline"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
uptodateresult = pipe.communicate()[0].decode().strip()

if(len(uptodateresult) != 0):
    print(uptodateresult)
    sys.exit(-1)

pipe = subprocess.Popen(["git", "rev-parse", "--show-toplevel"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
maindir = pipe.communicate()[0].decode().strip()


print("repository: "+maindir)

pipe = subprocess.Popen(["git", "describe", "--abbrev=0", "--tags"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
versionresult = pipe.communicate()[0].decode().strip()

print("last version: "+versionresult )
currentv = extractnumbers(versionresult)
if(len(sys.argv) != 2):
    nextv = (currentv[0],currentv[1], currentv[2]+1)
    print ("please specify version number, e.g. "+toversionstring(*nextv))
    sys.exit(-1)
try:
    newversion = extractnumbers(sys.argv[1])
except:
    print("can't parse version number "+sys.argv[1])
    sys.exit(-1)

print("checking that new version is valid")

if(newversion[0] !=  currentv[0]):
    assert newversion[0] ==  currentv[0] + 1
    assert newversion[1] == 0
    assert newversion[2] == 0
elif (newversion[1] !=  currentv[1]):
    assert newversion[1] ==  currentv[1] + 1
    assert newversion[2] == 0
else :
    assert newversion[2] ==  currentv[2] + 1



versionfilerel = os.sep + "include" + os.sep + "roaring" + os.sep + "roaring_version.h"
versionfile = maindir + versionfilerel

with open(versionfile, 'w') as file:
    file.write("// "+versionfilerel+" automatically generated by release.py, do not change by hand \n")
    file.write("#ifndef ROARING_INCLUDE_ROARING_VERSION \n")
    file.write("#define ROARING_INCLUDE_ROARING_VERSION \n")
    file.write("#define ROARING_VERSION = "+toversionstring(*newversion)+",  \n")
    file.write("enum { \n")
    file.write("    ROARING_VERSION_MAJOR = "+str(newversion[0])+",  \n")
    file.write("    ROARING_VERSION_MINOR = "+str(newversion[1])+",  \n")
    file.write("    ROARING_VERSION_REVISION = "+str(newversion[2])+"  \n")
    file.write("}; \n")
    file.write("#endif // ROARING_INCLUDE_ROARING_VERSION \n")


print(versionfile + " modified")

scriptlocation = os.path.dirname(os.path.abspath(__file__))

import fileinput
import re

newmajorversionstring = str(newversion[0])
mewminorversionstring = str(newversion[1])
newrevversionstring = str(newversion[2])
newversionstring = str(newversion[0]) + "." + str(newversion[1]) + "." + str(newversion[2])
cmakefile = maindir + os.sep + "CMakeLists.txt"
for line in fileinput.input(cmakefile, inplace=1, backup='.bak'):
    line = re.sub('ROARING_LIB_VERSION "\d+\.\d+\.\d+','ROARING_LIB_VERSION "'+newversionstring, line.rstrip())
    line = re.sub('ROARING_LIB_SOVERSION "\d+','ROARING_LIB_SOVERSION "'+newmajorversionstring, line)
    line = re.sub('set\(PROJECT_VERSION_MAJOR \d+','set(PROJECT_VERSION_MAJOR '+newmajorversionstring, line)
    line = re.sub('set\(PROJECT_VERSION_MINOR \d+','set(PROJECT_VERSION_MINOR '+mewminorversionstring, line)
    line = re.sub('set\(PROJECT_VERSION_PATCH \d+','set(PROJECT_VERSION_PATCH '+newrevversionstring, line)
    print(line)

print("modified "+cmakefile+", a backup was made")


print("Please run the tests before issuing a release: "+scriptlocation + "/prereleasetests.sh \n")
print("to issue release, enter \n git commit -a \n git push \n git tag -a v"+toversionstring(*newversion)+" -m \"version "+toversionstring(*newversion)+"\"\n git push --tags \n")
