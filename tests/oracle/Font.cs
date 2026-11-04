// --png2font <png> <out.font> [<png> <out.font>]...: the reference's own FontConvertor.ConvertFont.
// It loads the PNG through Texture2D.FromStream, which needs a GraphicsDevice: a bare Game opens
// one (a window flashes, as when the reference ran -png2font), converts every pair, and exits.
using Continuum93.Emulator;
using Continuum93.Utils;
using Microsoft.Xna.Framework;

static class Font
{
    public static int Run(string[] pairs)
    {
        using var game = new Converter(pairs);
        game.Run();
        return game.Failed;
    }

    sealed class Converter : Game
    {
        readonly string[] _pairs;
        public int Failed;

        public Converter(string[] pairs)
        {
            _pairs = pairs;
            Renderer.RegisterGraphicsDeviceManager(new GraphicsDeviceManager(this), this);
        }

        protected override void Initialize()
        {
            base.Initialize();   // the GraphicsDevice exists from here on
            for (int i = 0; i + 1 < _pairs.Length; i += 2)
            {
                try { File.WriteAllBytes(_pairs[i + 1], FontConvertor.ConvertFont(_pairs[i])); }
                catch (Exception e) { Console.Error.WriteLine($"{_pairs[i]}: {e.Message}"); Failed = 1; }
            }
            Exit();
        }
    }
}
