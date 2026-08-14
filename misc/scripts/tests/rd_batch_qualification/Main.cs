using System.Linq;
using Godot;
using ImageArray = Godot.Collections.Array<Godot.Image>;
using RectArray = Godot.Collections.Array<Godot.Rect2I>;

public partial class Main : Node
{
    public override void _Ready()
    {
        var failures = 0;
        var renderingDevice = RenderingServer.GetRenderingDevice();
        Check(renderingDevice is not null, "The active renderer did not expose a RenderingDevice.", ref failures);
        Check(RenderingServer.GetCurrentRenderingDriverName().Contains("vulkan", System.StringComparison.OrdinalIgnoreCase),
            "The active rendering driver is not Vulkan.", ref failures);
        Check(RenderingServer.TextureDrawableGetMaxArrayLayers() >= 3,
            "The active renderer does not support the required drawable array layers.", ref failures);

        if (failures == 0)
        {
            QualifyBatchedUploads(ref failures);
        }

        if (failures == 0)
        {
            GD.Print($"OUTPOSTIA_RD_BATCH_QUALIFICATION_PASS driver={RenderingServer.GetCurrentRenderingDriverName()} api={RenderingServer.GetVideoAdapterApiVersion()} device={renderingDevice?.GetDeviceName()}");
        }
        GetTree().Quit(failures == 0 ? 0 : 1);
    }

    private static void QualifyBatchedUploads(ref int failures)
    {
        var texture = new DrawableTexture2DArray();
        Check(texture.Setup(8, 8, 3, DrawableTexture2D.DrawableFormat.Rgba8, Colors.Transparent, true) == Error.Ok,
            "Drawable array setup failed.", ref failures);
        if (failures != 0)
        {
            return;
        }

        var generation = texture.GetGeneration();
        var images = new ImageArray
        {
            SolidImage(2, 2, Colors.Red),
            SolidImage(2, 2, Colors.Green),
            SolidImage(2, 1, Colors.Blue),
        };
        var regions = new RectArray
        {
            new Rect2I(0, 0, 2, 2),
            new Rect2I(2, 0, 2, 2),
            new Rect2I(1, 2, 2, 1),
        };
        int[] mipmaps = [0, 0, 1];
        int[] layers = [0, 0, 2];

        Check(RenderingServer.TextureDrawableUpdateSubresources(texture.GetRid(), images, regions, mipmaps, layers, generation) == Error.Ok,
            "The disjoint layered/mipmap batch was rejected.", ref failures);
        RenderingServer.ForceSync();

        var layerZero = RenderingServer.TextureDrawableGetSubresource(texture.GetRid(), 0, generation, 0);
        var layerTwoMipmap = RenderingServer.TextureDrawableGetSubresource(texture.GetRid(), 1, generation, 2);
        Check(layerZero is not null, "Layer-zero readback failed.", ref failures);
        Check(layerTwoMipmap is not null, "Layer-two mipmap readback failed.", ref failures);
        if (layerZero is not null)
        {
            Check(layerZero.GetPixel(0, 0).IsEqualApprox(Colors.Red), "The first disjoint region did not reach layer zero.", ref failures);
            Check(layerZero.GetPixel(2, 0).IsEqualApprox(Colors.Green), "The second disjoint region did not reach layer zero.", ref failures);
            Check(layerZero.GetPixel(4, 0).IsEqualApprox(Colors.Transparent), "A disjoint upload changed untouched bytes.", ref failures);
        }
        if (layerTwoMipmap is not null)
        {
            Check(layerTwoMipmap.GetPixel(1, 2).IsEqualApprox(Colors.Blue), "The explicit layer-two mipmap upload was not read back.", ref failures);
        }

        var beforeOverlap = RenderingServer.TextureDrawableGetSubresource(texture.GetRid(), 0, generation, 1);
        Check(beforeOverlap is not null, "Pre-overlap readback failed.", ref failures);
        var overlapImages = new ImageArray
        {
            SolidImage(2, 2, Colors.Yellow),
            SolidImage(2, 2, Colors.Magenta),
        };
        var overlapRegions = new RectArray
        {
            new Rect2I(0, 0, 2, 2),
            new Rect2I(1, 1, 2, 2),
        };
        int[] overlapMipmaps = [0, 0];
        int[] overlapLayers = [1, 1];
        Check(RenderingServer.TextureDrawableUpdateSubresources(texture.GetRid(), overlapImages, overlapRegions, overlapMipmaps, overlapLayers, generation) == Error.InvalidParameter,
            "An overlapping same-layer/mipmap batch was not rejected.", ref failures);
        RenderingServer.ForceSync();
        var afterOverlap = RenderingServer.TextureDrawableGetSubresource(texture.GetRid(), 0, generation, 1);
        Check(beforeOverlap is not null && afterOverlap is not null && beforeOverlap.GetData().SequenceEqual(afterOverlap.GetData()),
            "Overlap rejection mutated the destination.", ref failures);

        Check(RenderingServer.TextureDrawableUpdateSubresources(texture.GetRid(), images, regions, new[] { 0 }, layers, generation) == Error.InvalidParameter,
            "A mismatched batch was not rejected.", ref failures);
        Check(RenderingServer.TextureDrawableUpdateSubresources(texture.GetRid(), images, regions, mipmaps, layers, generation + 1) == Error.InvalidData,
            "A stale generation was not rejected.", ref failures);
    }

    private static Image SolidImage(int width, int height, Color color)
    {
        var image = Image.CreateEmpty(width, height, false, Image.Format.Rgba8);
        image.Fill(color);
        return image;
    }

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
