OH BTW this plugin is likely to be renamed  Inst8ball or Insta8Ball or Intest8Ball unless I come up with a better name, I just think as the plugin becomes sort of useable I will need to totally not name it Instacomposer Clone to avoid people accidentally mistaking this plugin for instacomposer - although the CLONE should probably be commonsense it is not instacomposer there are people who maybe don't speak English or something not sure but once I figure out how to change the project name I'll likely rename it. Since it also requires updating the source instances it will take some time to do that. The name was never actually on the plugin itself though so not a super high priority but on the todo 

to do get the rename done to Insta8Ball inless I come up with something better before then.

10-22-2026 
I added FL Studio FSC support so scores can be dragged to and from the plugin. A bunch of other small fixes. It was actually a bit of a rewrite since the last update on github.
There is actually a design plan for a method of storing the midi file as a type of complex formula that can contain the overall midi file as a representation within the formula.
This is a way of creating absolute values for midi files rather than representations of the midi file data itself. None the less, the plugin is still working.
It is musical. I've added the ability to create libraries and transforms and get data from midi files. 
I have to do some testing of the plugin to get a sense for how it is functioning but this is a structural update from version 1.
The actual midi files being generated as musical, I may need to improve the theory a bit for genre specific generation, need to give that some thought.
It will take me a while to use the royalty free midi libraries, to train transforms and see if there are any good sets to train from to get my view of specific genres.
This will likely take a while so no expected update in the near future as a lot of use and testing needs to ooccur. 

10-19-2026
To do add build up and syncopation correlation and more tracks... particularly way of splitting tracks and rythm.  the buildups is something that is important also thought to add specific "effects"  
tracks rather than just foilage. Still lots of bufa in rhia... also to create "split" track view or multitrack view rather than overlap.
Still no intention to integrate a sound engine any time soon.  Need to work on maping the plugin to fl studio flex instruments need to figure out how that works in fl studios native midi player.

10-18-2026 how time flies
mild theory and fl studio tiein updates still in building still havn't finished building but just updating as it has improved alittle
I am currently sort of focusing on theory improvements which is sort of going to need to be experiemnting I wanted to add more 
drum capability, particularly with loading kits direct from midi in fl studio its still not working but started to put that in.
(Although I am just a little impressed with what this thing is coming up with.... I actually liked the first two things this put out ... out of two well
well if life hands you lemons dream of other yellow things) cause everyone loves the golden things, golden hair flaxen and playen wax. More to come. :) 
Well unless the end arrives first :)


TO DO - get it to take midi inputs and theorize that input to the transmutation genre extenstion etc.. if possible get midi to feed into from channels... fl studio pianno roll etc.. would likely 
be doable via ara if fl studio had it but maybe I can figure something out.


This is NOT instacomposer!!!!!   (Instacomposer is still way better than this and likely will remain that way :)

This project was inspired by instacomposer and othe programs that can transfer midi data from a plugin to the daw.

I wanted to try doing something that is a generator that can transfer the data from the plugin.

It met that milestone.

I would like to develop this more but no real timeline on it.

Instacoposer itself is FAR superior to this plugin, this was more a demonstrator on how you can
transfer midi data from a plugin to a daw.

There are tons of things that can be added to this. 
Somewhat of a novelty for me as instacomposer 4  is on sale for $30 early release currently and likely a much better deal
May upgrade from instacomposer 2 to 4 


Inspired by https://www.waproduction.com/plugins/view/instacomposer-4 

Still not even close to its capabilities :)

No reverse engineering of instacomposer was done to make this plugin just in case you are wondering.


This is still a little akward on some of the note interfaces but its a nice start to more of a piano roll like interface with right click functionality for other uses.  I should see if I can get fl py working with it, maybe pianoroll scripts can be bridged into this plugin also???
