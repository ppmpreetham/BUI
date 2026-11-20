// SPDX-License-Identifier: GPL-2.0-or-later

package bui

/*
#include "glue.h"
*/
import "C"

import (
	"errors"
	"unsafe"
)

// CSS object-fit, for images fitted into a box.
const (
	FitFill      = C.BK_FIT_FILL
	FitContain   = C.BK_FIT_CONTAIN
	FitCover     = C.BK_FIT_COVER
	FitNone      = C.BK_FIT_NONE
	FitScaleDown = C.BK_FIT_SCALE_DOWN
)

// ImBuf is a decoded image (PNG, JPEG, WebP, GIF, BMP, TGA, QOI, HDR, PNM, SVG),
// premultiplied 0xAARRGGBB, with its frames.
type ImBuf struct{ h *C.BKImBuf }

var errDecode = errors.New("bui: not an image BUI reads")

// DecodeImage decodes image data, fitted within maxW x maxH (0: no limit).
func DecodeImage(data []byte, maxW, maxH int) (*ImBuf, error) {
	if len(data) == 0 {
		return nil, errDecode
	}
	h := C.BK_imbuf_decode(unsafe.Pointer(&data[0]), C.size_t(len(data)), C.int(maxW), C.int(maxH))
	if h == nil {
		return nil, errDecode
	}
	return &ImBuf{h}, nil
}

// LoadImage decodes an image file.
func LoadImage(path string, maxW, maxH int) (*ImBuf, error) {
	h := cstr(path, func(s *C.char) *C.BKImBuf { return C.BK_imbuf_load(s, C.int(maxW), C.int(maxH)) })
	if h == nil {
		return nil, errDecode
	}
	return &ImBuf{h}, nil
}

// ImageFromRGBA copies straight RGBA pixels (stride bytes per row).
func ImageFromRGBA(rgba []byte, w, h, stride int) *ImBuf {
	// every row holds its w pixels, and the last ends inside the slice
	if w <= 0 || h <= 0 || stride < w*4 || len(rgba) < (h-1)*stride+w*4 {
		return nil
	}
	return &ImBuf{C.BK_imbuf_from_rgba((*C.uchar)(unsafe.Pointer(&rgba[0])), C.int(w), C.int(h), C.int(stride))}
}

// Free frees the image; nodes showing it must let go first.
func (b *ImBuf) Free() {
	if b != nil && b.h != nil {
		C.BK_imbuf_free(b.h)
		b.h = nil
	}
}

// Size is the image's size and frame count.
func (b *ImBuf) Size() (w, h, frames int) {
	var cw, ch C.int
	frames = int(C.BK_imbuf_size(b.h, &cw, &ch))
	return int(cw), int(ch), frames
}

// SetImage shows straight RGBA pixels (copied) on a label, a button or an "image" node.
func (n *Node) SetImage(rgba []byte, w, h int) {
	if len(rgba) < w*h*4 || w <= 0 || h <= 0 {
		C.BK_node_set_image(n.h, nil, 0, 0)
		return
	}
	C.BK_node_set_image(n.h, (*C.uchar)(unsafe.Pointer(&rgba[0])), C.int(w), C.int(h))
}

// SetImBuf shows a decoded image, which must outlive the node's use of it.
func (n *Node) SetImBuf(b *ImBuf) {
	var h *C.BKImBuf
	if b != nil {
		h = b.h
	}
	C.BK_node_set_imbuf(n.h, h)
}

// SetImageSource shows an image file, decoded in the background at the size
// it is drawn, fitted by fit (Fit*).
func (n *Node) SetImageSource(src string, fit int) {
	withCstr(src, func(s *C.char) { C.BK_node_set_image_source(n.h, s, C.int(fit)) })
}

// SetImageFrame shows frame n-1 of an animated source (0 plays it).
func (n *Node) SetImageFrame(frame int) { C.BK_node_set_image_frame(n.h, C.int(frame)) }
