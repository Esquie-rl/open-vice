//! Open Vice: the frame rate drawn into every presented image, top left.
//!
//! The text is drawn by GDI (an antialiased sans-serif face, white), given a
//! black outline by widening its coverage, and kept as a premultiplied RGBA
//! texture with nothing around it. After the title's image is copied into the
//! back buffer, one quad draws the texture over it with premultiplied alpha
//! blending, so only the letters and their outline cover the image. The
//! texture is redrawn only when the text changes (twice a second).

use windows::Win32::Foundation::COLORREF;
use windows::Win32::Graphics::Direct3D::Fxc::D3DCompile;
use windows::Win32::Graphics::Direct3D::{D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, ID3DBlob};
use windows::Win32::Graphics::Direct3D12::*;
use windows::Win32::Graphics::Dxgi::Common::*;
use windows::Win32::Graphics::Gdi::*;
use windows::core::{PCSTR, s, w};

const SOURCE: &str = r#"
#define RS "RootConstants(num32BitConstants=4, b0), DescriptorTable(SRV(t0)), StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_POINT, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP)"
cbuffer Place : register(b0) { float4 rect; };
Texture2D text : register(t0);
SamplerState pick : register(s0);
struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };
[RootSignature(RS)]
V vs(uint id : SV_VertexID) {
    float2 uv = float2(id & 1, id >> 1);
    V o;
    o.p = float4(lerp(rect.x, rect.z, uv.x), lerp(rect.y, rect.w, uv.y), 0, 1);
    o.uv = uv;
    return o;
}
[RootSignature(RS)]
float4 ps(V i) : SV_Target { return text.Sample(pick, i.uv); }
"#;

/// What one swap chain needs to draw the text: the texture and its upload
/// buffer, the descriptors, and a pipeline for the back buffer's format.
pub(crate) struct Overlay {
    root: ID3D12RootSignature,
    pso: ID3D12PipelineState,
    format: DXGI_FORMAT,
    texture: ID3D12Resource,
    upload: ID3D12Resource,
    /// The upload buffer's mapping (an address: presenters cross threads).
    upload_data: usize,
    footprint: D3D12_PLACED_SUBRESOURCE_FOOTPRINT,
    srv_heap: ID3D12DescriptorHeap,
    rtv_heap: ID3D12DescriptorHeap,
    /// The font height the texture was sized for, in pixels.
    px: i32,
    /// What the texture holds, and whether it has been copied there yet.
    text: String,
    copied: bool,
}

/// The font height for a back buffer this tall.
fn font_px(height: u32) -> i32 {
    (height as i32 / 28).max(14)
}

/// The text as a premultiplied RGBA image `w` x `h`: white letters, a black
/// outline `px / 14` pixels wide (at least 2), transparent elsewhere.
fn rasterize(text: &str, px: i32, w: u32, h: u32) -> Vec<u8> {
    let (wu, hu) = (w as usize, h as usize);
    let mut coverage = vec![0u8; wu * hu];
    // SAFETY: GDI objects made here, selected into our own memory DC and
    // released before returning; the DIB's bits are read while it is alive.
    unsafe {
        let dc = CreateCompatibleDC(None);
        let info = BITMAPINFO {
            bmiHeader: BITMAPINFOHEADER {
                biSize: size_of::<BITMAPINFOHEADER>() as u32,
                biWidth: w as i32,
                biHeight: -(h as i32),
                biPlanes: 1,
                biBitCount: 32,
                biCompression: BI_RGB.0,
                ..Default::default()
            },
            ..Default::default()
        };
        let mut bits = std::ptr::null_mut();
        if let Ok(bitmap) = CreateDIBSection(Some(dc), &info, DIB_RGB_COLORS, &mut bits, None, 0) {
            let old_bitmap = SelectObject(dc, bitmap.into());
            let font = CreateFontW(
                -px,
                0,
                0,
                0,
                FW_SEMIBOLD.0 as i32,
                0,
                0,
                0,
                DEFAULT_CHARSET,
                OUT_TT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                ANTIALIASED_QUALITY,
                (DEFAULT_PITCH.0 | FF_SWISS.0) as u32,
                w!("Segoe UI"),
            );
            let old_font = SelectObject(dc, font.into());
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, COLORREF(0x00ff_ffff));
            let wide: Vec<u16> = text.encode_utf16().collect();
            let margin = outline(px) + 1;
            let _ = TextOutW(dc, margin, margin, &wide);
            let _ = GdiFlush();
            let pixels = std::slice::from_raw_parts(bits.cast::<u8>(), wu * hu * 4);
            for (c, p) in coverage.iter_mut().zip(pixels.as_chunks::<4>().0) {
                *c = p[0].max(p[1]).max(p[2]);
            }
            SelectObject(dc, old_font);
            SelectObject(dc, old_bitmap);
            let _ = DeleteObject(font.into());
            let _ = DeleteObject(bitmap.into());
        }
        let _ = DeleteDC(dc);
    }
    // The outline: the coverage widened by a disc of radius r.
    let r = outline(px);
    let mut wide = vec![0u8; wu * hu];
    for y in 0..hu as i32 {
        for x in 0..wu as i32 {
            let mut m = 0u8;
            for dy in -r..=r {
                for dx in -r..=r {
                    if dx * dx + dy * dy > r * r + r {
                        continue;
                    }
                    let (sx, sy) = (x + dx, y + dy);
                    if sx >= 0 && sy >= 0 && (sx as usize) < wu && (sy as usize) < hu {
                        m = m.max(coverage[sy as usize * wu + sx as usize]);
                    }
                }
            }
            wide[y as usize * wu + x as usize] = m;
        }
    }
    let mut rgba = vec![0u8; wu * hu * 4];
    for ((px, &c), &o) in rgba.as_chunks_mut::<4>().0.iter_mut().zip(&coverage).zip(&wide) {
        let (c, o) = (f32::from(c) / 255.0, f32::from(o) / 255.0);
        let a = c + o * (1.0 - c);
        let v = (c * 255.0).round() as u8; // white, premultiplied
        px.copy_from_slice(&[v, v, v, (a * 255.0).round() as u8]);
    }
    rgba
}

fn outline(px: i32) -> i32 {
    (px / 14).max(2)
}

/// The texture's size for a font height: room for "000.0 FPS | 0000.00 ms".
fn texture_size(px: i32) -> (u32, u32) {
    let margin = 2 * (outline(px) + 1);
    ((px * 12 + margin) as u32, (px * 3 / 2 + margin) as u32)
}

fn compile(entry: PCSTR, target: PCSTR) -> windows::core::Result<ID3DBlob> {
    let mut code: Option<ID3DBlob> = None;
    let mut errors: Option<ID3DBlob> = None;
    // SAFETY: the source and names live through the call; out pointers ours.
    let compiled = unsafe {
        D3DCompile(
            SOURCE.as_ptr().cast(),
            SOURCE.len(),
            s!("overlay"),
            None,
            None,
            entry,
            target,
            0,
            0,
            &mut code,
            Some(&mut errors),
        )
    };
    if let Err(e) = compiled {
        if let Some(errors) = errors {
            // SAFETY: the blob's own bytes.
            let text = unsafe {
                std::slice::from_raw_parts(errors.GetBufferPointer().cast::<u8>(), errors.GetBufferSize())
            };
            tracing::warn!("overlay shader: {}", String::from_utf8_lossy(text));
        }
        return Err(e);
    }
    code.ok_or_else(windows::core::Error::empty)
}

fn bytes(blob: &ID3DBlob) -> &[u8] {
    // SAFETY: the blob's own bytes, alive as long as the blob.
    unsafe { std::slice::from_raw_parts(blob.GetBufferPointer().cast::<u8>(), blob.GetBufferSize()) }
}

impl Overlay {
    /// Everything for back buffers of `format` and `height`; `None` (logged)
    /// if a part cannot be made, and the frame rate is then not drawn.
    pub(crate) fn new(
        device: &ID3D12Device,
        format: DXGI_FORMAT,
        height: u32,
        upload: impl Fn(u64) -> Option<(ID3D12Resource, *mut u8)>,
    ) -> Option<Self> {
        let made = (|| -> windows::core::Result<Self> {
            let vs = compile(s!("vs"), s!("vs_5_1"))?;
            let ps = compile(s!("ps"), s!("ps_5_1"))?;
            let px = font_px(height);
            let (w, h) = texture_size(px);
            // SAFETY: plain object creation with descriptions that live through each call.
            unsafe {
                let root: ID3D12RootSignature = device.CreateRootSignature(0, bytes(&vs))?;
                let mut blend = D3D12_BLEND_DESC::default();
                blend.RenderTarget[0] = D3D12_RENDER_TARGET_BLEND_DESC {
                    BlendEnable: true.into(),
                    LogicOpEnable: false.into(),
                    SrcBlend: D3D12_BLEND_ONE,
                    DestBlend: D3D12_BLEND_INV_SRC_ALPHA,
                    BlendOp: D3D12_BLEND_OP_ADD,
                    SrcBlendAlpha: D3D12_BLEND_ONE,
                    DestBlendAlpha: D3D12_BLEND_INV_SRC_ALPHA,
                    BlendOpAlpha: D3D12_BLEND_OP_ADD,
                    LogicOp: D3D12_LOGIC_OP_NOOP,
                    RenderTargetWriteMask: D3D12_COLOR_WRITE_ENABLE_ALL.0 as u8,
                };
                let mut desc = D3D12_GRAPHICS_PIPELINE_STATE_DESC {
                    pRootSignature: std::mem::transmute_copy(&root),
                    VS: D3D12_SHADER_BYTECODE {
                        pShaderBytecode: bytes(&vs).as_ptr().cast(),
                        BytecodeLength: bytes(&vs).len(),
                    },
                    PS: D3D12_SHADER_BYTECODE {
                        pShaderBytecode: bytes(&ps).as_ptr().cast(),
                        BytecodeLength: bytes(&ps).len(),
                    },
                    BlendState: blend,
                    SampleMask: u32::MAX,
                    RasterizerState: D3D12_RASTERIZER_DESC {
                        FillMode: D3D12_FILL_MODE_SOLID,
                        CullMode: D3D12_CULL_MODE_NONE,
                        DepthClipEnable: true.into(),
                        ..Default::default()
                    },
                    PrimitiveTopologyType: D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
                    NumRenderTargets: 1,
                    SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
                    ..Default::default()
                };
                desc.RTVFormats[0] = format;
                let pso: ID3D12PipelineState = device.CreateGraphicsPipelineState(&desc)?;

                let heap = D3D12_HEAP_PROPERTIES {
                    Type: D3D12_HEAP_TYPE_DEFAULT,
                    ..Default::default()
                };
                let tdesc = D3D12_RESOURCE_DESC {
                    Dimension: D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                    Width: u64::from(w),
                    Height: h,
                    DepthOrArraySize: 1,
                    MipLevels: 1,
                    Format: DXGI_FORMAT_R8G8B8A8_UNORM,
                    SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
                    Layout: D3D12_TEXTURE_LAYOUT_UNKNOWN,
                    ..Default::default()
                };
                let mut texture: Option<ID3D12Resource> = None;
                device.CreateCommittedResource(
                    &heap,
                    D3D12_HEAP_FLAG_NONE,
                    &tdesc,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    None,
                    &mut texture,
                )?;
                let texture = texture.ok_or_else(windows::core::Error::empty)?;
                let pitch = (w * 4).div_ceil(256) * 256;
                let (upload, data) =
                    upload(u64::from(pitch) * u64::from(h)).ok_or_else(windows::core::Error::empty)?;
                let footprint = D3D12_PLACED_SUBRESOURCE_FOOTPRINT {
                    Offset: 0,
                    Footprint: D3D12_SUBRESOURCE_FOOTPRINT {
                        Format: DXGI_FORMAT_R8G8B8A8_UNORM,
                        Width: w,
                        Height: h,
                        Depth: 1,
                        RowPitch: pitch,
                    },
                };
                let srv_heap: ID3D12DescriptorHeap = device.CreateDescriptorHeap(&D3D12_DESCRIPTOR_HEAP_DESC {
                    Type: D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                    NumDescriptors: 1,
                    Flags: D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                    NodeMask: 0,
                })?;
                device.CreateShaderResourceView(&texture, None, srv_heap.GetCPUDescriptorHandleForHeapStart());
                let rtv_heap: ID3D12DescriptorHeap = device.CreateDescriptorHeap(&D3D12_DESCRIPTOR_HEAP_DESC {
                    Type: D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                    NumDescriptors: 1,
                    Flags: D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
                    NodeMask: 0,
                })?;
                Ok(Overlay {
                    root,
                    pso,
                    format,
                    texture,
                    upload,
                    upload_data: data as usize,
                    footprint,
                    srv_heap,
                    rtv_heap,
                    px,
                    text: String::new(),
                    copied: false,
                })
            }
        })();
        match made {
            Ok(o) => Some(o),
            Err(e) => {
                tracing::warn!(error = %e, "no frame-rate overlay");
                None
            }
        }
    }

    /// Made for back buffers of this format and height.
    pub(crate) fn fits(&self, format: DXGI_FORMAT, height: u32) -> bool {
        self.format == format && self.px == font_px(height)
    }

    /// Records, in `list`, the text drawn over `back` (in COPY_DEST, the
    /// title's image just copied there; left in RENDER_TARGET), redrawing
    /// the texture first if `text` changed.
    ///
    /// # Safety
    /// `list` must be open and `device` the one the overlay was made on;
    /// the list's previous submission must have finished (the upload buffer
    /// is rewritten).
    pub(crate) unsafe fn record(
        &mut self,
        device: &ID3D12Device,
        list: &ID3D12GraphicsCommandList,
        back: &ID3D12Resource,
        (width, height): (u32, u32),
        text: &str,
        transition: impl Fn(&ID3D12Resource, D3D12_RESOURCE_STATES, D3D12_RESOURCE_STATES) -> D3D12_RESOURCE_BARRIER,
    ) {
        let f = self.footprint.Footprint;
        // SAFETY: per this function's contract; every pointer is ours.
        unsafe {
            if self.text != text {
                let rgba = rasterize(text, self.px, f.Width, f.Height);
                let row = f.Width as usize * 4;
                for y in 0..f.Height as usize {
                    std::ptr::copy_nonoverlapping(
                        rgba.as_ptr().add(y * row),
                        (self.upload_data + y * f.RowPitch as usize) as *mut u8,
                        row,
                    );
                }
                if self.copied {
                    list.ResourceBarrier(&[transition(
                        &self.texture,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_COPY_DEST,
                    )]);
                }
                let to = D3D12_TEXTURE_COPY_LOCATION {
                    pResource: std::mem::transmute_copy(&self.texture),
                    Type: D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                    Anonymous: D3D12_TEXTURE_COPY_LOCATION_0 { SubresourceIndex: 0 },
                };
                let from = D3D12_TEXTURE_COPY_LOCATION {
                    pResource: std::mem::transmute_copy(&self.upload),
                    Type: D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                    Anonymous: D3D12_TEXTURE_COPY_LOCATION_0 { PlacedFootprint: self.footprint },
                };
                list.CopyTextureRegion(&to, 0, 0, 0, &from, None);
                list.ResourceBarrier(&[transition(
                    &self.texture,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                )]);
                self.text = text.to_owned();
                self.copied = true;
            }
            list.ResourceBarrier(&[transition(
                back,
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
            )]);
            let rtv = self.rtv_heap.GetCPUDescriptorHandleForHeapStart();
            device.CreateRenderTargetView(back, None, rtv);
            list.SetDescriptorHeaps(&[Some(self.srv_heap.clone())]);
            list.SetGraphicsRootSignature(&self.root);
            list.SetPipelineState(&self.pso);
            // The quad in clip space: the texture's own size, a small margin from the top left.
            let margin = (height / 90).max(4) as f32;
            let (bw, bh) = (width as f32, height as f32);
            let x0 = -1.0 + 2.0 * margin / bw;
            let y0 = 1.0 - 2.0 * margin / bh;
            let rect = [x0, y0, x0 + 2.0 * f.Width as f32 / bw, y0 - 2.0 * f.Height as f32 / bh];
            list.SetGraphicsRoot32BitConstants(0, 4, rect.as_ptr().cast(), 0);
            list.SetGraphicsRootDescriptorTable(1, self.srv_heap.GetGPUDescriptorHandleForHeapStart());
            list.RSSetViewports(&[D3D12_VIEWPORT {
                TopLeftX: 0.0,
                TopLeftY: 0.0,
                Width: bw,
                Height: bh,
                MinDepth: 0.0,
                MaxDepth: 1.0,
            }]);
            list.RSSetScissorRects(&[windows::Win32::Foundation::RECT {
                left: 0,
                top: 0,
                right: width as i32,
                bottom: height as i32,
            }]);
            list.OMSetRenderTargets(1, Some(&rtv), false, None);
            list.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            list.DrawInstanced(4, 1, 0, 0);
        }
    }
}
