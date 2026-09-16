/*
 * Predicted weapon sounds, and suppression of the server's echo of them.
 *
 * When we predict a shot we play its sound immediately. The server plays the
 * same sound a round trip later, and we have to drop that copy or the player
 * hears everything twice.
 *
 * We do that with one-shot tokens rather than a permanent "always suppress
 * these sounds" list: firing pushes a token, and the first matching server
 * sound consumes it. A sound we did not predict, because prediction was off or
 * disagreed with the server, still reaches the player. Tokens expire so a
 * prediction the server never confirms cannot silence a later real sound.
 */

string	token_name[MAX_SOUND_TOKENS];
float	token_chan[MAX_SOUND_TOKENS];
float	token_expires[MAX_SOUND_TOKENS];

void Sound_Reset(void)
{
	float i;

	for (i = 0; i < MAX_SOUND_TOKENS; i++)
	{
		token_name[i] = "";
		token_chan[i] = 0;
		token_expires[i] = 0;
	}
}

static void Sound_PushToken(string sample, float chan)
{
	float i, oldest = 0;

	for (i = 0; i < MAX_SOUND_TOKENS; i++)
	{
		if (token_expires[i] <= cltime)
		{
			oldest = i;
			break;
		}

		// All slots live: overwrite whichever expires first.
		if (token_expires[i] < token_expires[oldest])
			oldest = i;
	}

	token_name[oldest] = sample;
	token_chan[oldest] = chan;
	token_expires[oldest] = cltime + SOUND_TOKEN_TTL;
}

// Play a sound we are predicting, and arrange for the server's copy to be
// dropped when it arrives.
void Sound_PlayPredicted(string sample, float chan)
{
	if (sample == "")
		return;

	if (!Pred_SoundEnabled())
		return;

	Sound_PushToken(sample, chan);

	// ATTN_NONE: this is our own weapon, it should not fall off with distance.
	sound(viewweapon, chan, sample, 1, ATTN_NONE);
}

/*
 * The engine asks before playing any server sound. Returning true drops it.
 */
float CSQC_Event_Sound(float entnum, float channel, string soundname,
                       float vol, float attenuation, vector pos,
                       float pitchmod, float flags)
{
	float i;

	if (!Pred_Enabled() || !Pred_SoundEnabled())
		return FALSE;

	// Nothing was predicted for this state, so the server's sound is the only one.
	if (!Weapon_Predictable(ws_server.predflags))
		return FALSE;

	// Only ever silence our own weapon. While spectating, player_localentnum is
	// the tracked player, whose sounds we are not predicting.
	if (!player_playable || entnum != player_localentnum)
		return FALSE;

	for (i = 0; i < MAX_SOUND_TOKENS; i++)
	{
		if (token_expires[i] <= cltime)
			continue;
		if (token_name[i] != soundname)
			continue;

		// A token on CHAN_AUTO was played on a channel of the engine's choosing,
		// so accept the sound whatever channel it arrives on.
		if (token_chan[i] != channel && token_chan[i] != CHAN_AUTO)
			continue;

		token_expires[i] = 0;
		return TRUE;
	}

	return FALSE;
}
