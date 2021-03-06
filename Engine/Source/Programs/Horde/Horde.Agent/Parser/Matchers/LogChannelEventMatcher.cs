// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text.RegularExpressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Parser.Matchers
{
	/// <summary>
	/// Matches events formatted as UE log channel output
	/// </summary>
	class LogChannelEventMatcher : ILogEventMatcher
	{
		/// <inheritdoc/>
		public LogEventMatch? Match(ILogCursor input)
		{
			Match? match;
			if(input.TryMatch(@"^(\s*)(?<channel>[a-zA-Z_][a-zA-Z0-9_]*):\s*(?<severity>Error|Warning|Display): ", out match))
			{
				LogEventBuilder builder = new LogEventBuilder(input);
				builder.Annotate(match.Groups["channel"], LogEventMarkup.Channel);
				builder.Annotate(match.Groups["severity"], LogEventMarkup.Severity);
				
				if (builder.Next.TryMatch(@"^\s+", out Match? indent))
				{						
					while (builder.Next.IsMatch($"^{indent.Value}"))
					{
						builder.MoveNext();
					}					
				}

				LogLevel level = match.Groups["severity"].Value switch
				{
					"Error" => LogLevel.Error,
					"Warning" => LogLevel.Warning,
					_ => LogLevel.Information,
				};

				return builder.ToMatch(LogEventPriority.Low, level, KnownLogEvents.Engine_LogChannel);
				
			}
			return null;
		}
	}
}
