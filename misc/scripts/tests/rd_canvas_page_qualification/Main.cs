using System;
using System.Threading.Tasks;
using Godot;

public partial class Main : Node
{
    public override async void _Ready()
    {
        var failures = 0;
        var renderingDevice = RenderingServer.GetRenderingDevice();
        Check(renderingDevice is not null, "The active renderer did not expose a RenderingDevice.", ref failures);
        Check(RenderingServer.GetCurrentRenderingDriverName().Contains("vulkan", StringComparison.OrdinalIgnoreCase),
            "The active rendering driver is not Vulkan.", ref failures);
        Check(RenderingServer.TextureDrawableGetMaxArrayLayers() >= 2,
            "The active renderer does not support the required drawable array layers.", ref failures);

        if (failures == 0)
        {
            failures = await QualifyCanvasPages(failures);
        }

        if (failures == 0)
        {
            GD.Print($"OUTPOSTIA_RD_CANVAS_PAGE_QUALIFICATION_PASS driver={RenderingServer.GetCurrentRenderingDriverName()} api={RenderingServer.GetVideoAdapterApiVersion()} device={renderingDevice?.GetDeviceName()}");
        }
        GetTree().Quit(failures == 0 ? 0 : 1);
    }

    private async Task<int> QualifyCanvasPages(int failures)
    {
        var page = new DrawableTexture2D();
        Check(page.SetupChecked(8, 8, DrawableTexture2D.DrawableFormat.Rgba8, Colors.Transparent, true) == Error.Ok,
            "Drawable page setup failed.", ref failures);
        if (failures != 0)
        {
            return failures;
        }

        var generation = RenderingServer.TextureDrawableGetGeneration(page.GetRid());
        Check(Upload(page.GetRid(), generation, 0, 0, SolidImage(8, 8, Colors.Green)) == Error.Ok,
            "Base page upload failed.", ref failures);
        Check(Upload(page.GetRid(), generation, 1, 0, SolidImage(4, 4, Colors.Red)) == Error.Ok,
            "Page mipmap-one upload failed.", ref failures);
        Check(Upload(page.GetRid(), generation, 2, 0, SolidImage(2, 2, Colors.Blue)) == Error.Ok,
            "Page mipmap-two upload failed.", ref failures);

        var fallback = ImageTexture.CreateFromImage(SolidImage(8, 8, Colors.Magenta));
        var view = new CanvasTexturePageView();
        Check(view.Configure(page, new Rect2I(0, 0, 8, 8), 0, 0, generation, fallback) == Error.Ok,
            "Ordinary page view configuration failed.", ref failures);

        var material = ColorMultiplierMaterial();
        var unmodified = await Render(view, null, Colors.White, false, new Vector2I(8, 8), new Vector2(8, 8));
        var modulated = await Render(view, null, new Color(1, 0.5f, 1, 1), false, new Vector2I(8, 8), new Vector2(8, 8));
        var materialModified = await Render(view, material, new Color(1, 0.5f, 1, 1), false, new Vector2I(8, 8), new Vector2(8, 8));
        Check(IsGreen(unmodified), "The no-material page draw did not sample the page base level.", ref failures);
        Check(IsGreen(modulated) && modulated.G < unmodified.G * 0.8f,
            "CanvasItem modulation was not applied to the paged draw.", ref failures);
        Check(IsGreen(materialModified) && materialModified.G < modulated.G * 0.8f,
            "The COLOR-only material did not modify the already sampled page color.", ref failures);

        var lodView = new CanvasTexturePageView();
        Check(lodView.Configure(page, new Rect2I(0, 0, 8, 8), 0, 2, generation, fallback) == Error.Ok,
            "Mipmap page view configuration failed.", ref failures);
        var lod = await Render(lodView, null, Colors.White, false, new Vector2I(8, 8), new Vector2(8, 8));
        GD.Print($"OUTPOSTIA_RD_CANVAS_PAGE_LOD_SAMPLE color={lod}");
        Check(IsBlue(lod), "The paged draw did not select the configured lower mipmap.", ref failures);

        var fallbackResult = await Render(view, null, Colors.White, true, new Vector2I(8, 8), new Vector2(8, 8));
        Check(IsMagenta(fallbackResult), "Texture repeat did not retain the ordinary fallback path.", ref failures);

        var arrayPage = new DrawableTexture2DArray();
        Check(arrayPage.Setup(8, 8, 2, DrawableTexture2D.DrawableFormat.Rgba8, Colors.Transparent, true) == Error.Ok,
            "Drawable array page setup failed.", ref failures);
        if (failures != 0)
        {
            return failures;
        }
        var arrayGeneration = arrayPage.GetGeneration();
        Check(Upload(arrayPage.GetRid(), arrayGeneration, 0, 0, SolidImage(8, 8, Colors.Red)) == Error.Ok,
            "Array layer-zero upload failed.", ref failures);
        Check(Upload(arrayPage.GetRid(), arrayGeneration, 0, 1, SolidImage(8, 8, Colors.Cyan)) == Error.Ok,
            "Array layer-one upload failed.", ref failures);

        var arrayFallback = ImageTexture.CreateFromImage(SolidImage(8, 8, Colors.Yellow));
        var arrayView = new CanvasTexturePageView();
        Check(arrayView.Configure(arrayPage, new Rect2I(0, 0, 8, 8), 1, 0, arrayGeneration, arrayFallback) == Error.Ok,
            "Layered page view configuration failed.", ref failures);
        var arrayUnmodified = await Render(arrayView, null, Colors.White, false, new Vector2I(8, 8), new Vector2(8, 8));
        var arrayMaterialModified = await Render(arrayView, material, Colors.White, false, new Vector2I(8, 8), new Vector2(8, 8));
        Check(IsCyan(arrayUnmodified), "The layered page draw did not select the configured array layer.", ref failures);
        Check(IsCyan(arrayMaterialModified) && arrayMaterialModified.G < arrayUnmodified.G * 0.8f && arrayMaterialModified.B < arrayUnmodified.B * 0.8f,
            "The COLOR-only material did not modify the layered page sample.", ref failures);
        return failures;
    }

    private async Task<Color> Render(Texture2D texture, Material? material, Color modulate, bool repeat, Vector2I viewportSize, Vector2 drawSize)
    {
        var viewport = new SubViewport
        {
            Size = viewportSize,
            Disable3D = true,
            TransparentBg = false,
            RenderTargetClearMode = SubViewport.ClearMode.Always,
            RenderTargetUpdateMode = SubViewport.UpdateMode.Always,
        };
        var rect = new TextureRect
        {
            Texture = texture,
            Size = drawSize,
            ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
            StretchMode = TextureRect.StretchModeEnum.Scale,
            TextureFilter = CanvasItem.TextureFilterEnum.NearestWithMipmaps,
            TextureRepeat = repeat ? CanvasItem.TextureRepeatEnum.Enabled : CanvasItem.TextureRepeatEnum.Disabled,
            Modulate = modulate,
            Material = material,
        };
        viewport.AddChild(rect);
        AddChild(viewport);

        for (var frame = 0; frame < 4; frame++)
        {
            await ToSignal(RenderingServer.Singleton, RenderingServer.SignalName.FramePostDraw);
        }
        RenderingServer.ForceSync();
        var image = viewport.GetTexture().GetImage();
        var color = image.GetPixel(0, 0);
        viewport.RenderTargetUpdateMode = SubViewport.UpdateMode.Disabled;
        viewport.QueueFree();
        await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
        return color;
    }

    private static Error Upload(Rid texture, ulong generation, int mipmap, int layer, Image image)
    {
        return RenderingServer.TextureDrawableUpdateSubresource(texture, image, new Rect2I(0, 0, image.GetWidth(), image.GetHeight()), mipmap, generation, layer);
    }

    private static ShaderMaterial ColorMultiplierMaterial()
    {
        var shader = new Shader
        {
            Code = "shader_type canvas_item; render_mode unshaded; void fragment() { COLOR *= vec4(0.5, 0.5, 0.5, 1.0); }",
        };
        return new ShaderMaterial { Shader = shader };
    }

    private static Image SolidImage(int width, int height, Color color)
    {
        var image = Image.CreateEmpty(width, height, false, Image.Format.Rgba8);
        image.Fill(color);
        return image;
    }

    private static bool IsGreen(Color color) => color.G > 0.2f && color.R < 0.1f && color.B < 0.1f;
    private static bool IsBlue(Color color) => color.B > 0.5f && color.R < 0.1f && color.G < 0.1f;
    private static bool IsMagenta(Color color) => color.R > 0.5f && color.B > 0.5f && color.G < 0.1f;
    private static bool IsCyan(Color color) => color.G > 0.2f && color.B > 0.2f && color.R < 0.1f;

    private static void Check(bool condition, string message, ref int failures)
    {
        if (condition)
        {
            return;
        }
        failures++;
        GD.PushError(message);
    }
}
