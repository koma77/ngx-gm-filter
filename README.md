ngx-gm-filter
=============

ngx-gm-filter - Another image filter based GraphicsMagick.

Status
======

This module was successfully built and tested with nginx-1.11.4


Version
=======

This document describes ngx-gm-filter [v0.2.0](https://github.com/liseen/ngx-gm-filter/tags).


Synopsis
========

```
server {
    gm_buffer 10M;

    location /gm {
         alias imgs;

         set $resize 100x100>;
         set $rotate 180;
         set $fmt webp;

         gm convert -resize $resize -rotate $rotate -format $fmt;
         gm composite -gravity SouthEast -min-width 200 -min-height 200 wm.png;

         gm_image_quality 85;
    }
}
```

Description
===========

Directives
==========

gm_buffer
--------------
**syntax:** *gm_buffer size*

**default:** *gm_buffer 4M*

**context:** *server, location*


gm_image_quality
--------------
**syntax:** *gm_image_quality quality*

**default:** *gm_image_quality 75*

**context:** *server, location*

gm
--------------
**syntax:** *gm_[convert|composite|format]_ options*

**default:** *none*

**context:** *location*

gm samples
--------------

```
gm convert -resize 100x200!
gm convert -resize 100x200c
```


Installation
============

Install GraphicsMagick
------------


     sudo yum -y install GraphicsMagick-devel
     rpm -qa|grep Magick
     GraphicsMagick-devel-1.3.25-1.el7.x86_64
     GraphicsMagick-1.3.25-1.el7.x86_64

Install ngx-gm-filter
------------

Build the source with this module:

    wget 'http://nginx.org/download/nginx-1.11.4.tar.gz'
    tar -xzvf nginx-1.11.4.tar.gz
    cd nginx-1.11.4/

    ./configure --prefix=/opt/nginx \
				--add-module=path/to/ngx-gm-filter

    make -j2
    sudo make install


Copyright and License
=====================

This module is licensed under the BSD license.

Copyright (C) 2009-2012, by "liseen" Xunxin Wan(万珣新) <liseen.wan@gmail.com>.

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


See Also
========

* [GraphicsMagick](http://www.graphicsmagick.org/)  GraphicsMagick library.
* [HttpImageFilterModule](http://wiki.nginx.org/HttpImageFilterModule) .
