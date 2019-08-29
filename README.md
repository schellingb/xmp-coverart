# xmp-coverart
This plugin for [XMPlay](https://www.un4seen.com/xmplay.html)
displays album art of the playing song in the 'Visuals' panel.

The plugin also includes an automatic cover download feature.

## Download
You can find a binary download under the
[Releases page](https://github.com/schellingb/xmp-coverart/releases/latest).

## Screenshot
![Screenshot](https://raw.githubusercontent.com/schellingb/xmp-coverart/master/README.png)

## Usage
To run it, copy xmp-coverart.svp somewhere under the XMPlay directory and restart XMPlay.
In XMPlay press F9 (or select the Visuals panel), right click the window and
select the 'Cover Art' plugin from the list.

Supported are all image files found in the same directory as the song and all integrated images
in the ID3, FLAC or MP4/M4A tags of a playing audio file. Supported are the JPEG, PNG, BMP,
TGA, PSD and  GIF image formats. For songs in archives the cover images need to be in the
file tag or in a default cover image directory.

The configuration of the cover art can be accessed by pressing shift and clicking the
visuals screen. Alternatively it can be opened with a middle click.

You can then set up the following options:

 - Fade Album Art: Fading of all album changes. Should/could be disabled for bigger display.

 - Album Art alignment/stretch: Changes between different aligning and stretching options.

 - Auto cover downloader: When active, automatically downloads (and optionally saves) covers.

## Notes
 - Auto cycling through all available album art images is available in the 'Select Cover' menu.
   Can be customized by changing albumart_cycleseconds value in vis.ini under [coverart].

 - If saving of covers is enabled, the user can select a different cover after downloading 
   and chose 'Save selected downloaded cover' from the menu to keep one.

 - (Re)downloading can be done anytime by selecting 'Download now' in the menu to
   recheck covers or to select another one to save on disk.

 - Saving for covers coming from network streams or songs stored in archive files is available
   if a default save directory is enabled for the downloaded covers (check the menu option
   'auto save stream/archive' for that).

 - The plugin loads the file "cover.jpg" or "cover.png" from the same directory where the plugin
   SVP is stored as default cover image. If it does not exist, it shows the included image.
   It does the same for "download.jpg" or "download.png" to replace the image being displayed
   while the auto cover downloader is downloading.

## License
xmp-coverart available under the [Public Domain](https://www.unlicense.org).
