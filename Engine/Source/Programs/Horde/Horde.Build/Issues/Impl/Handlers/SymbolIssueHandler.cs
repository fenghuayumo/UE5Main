// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using EpicGames.Core;
using Horde.Build.Collections;
using Horde.Build.Models;
using Horde.Build.Utilities;
using Microsoft.Extensions.Logging;

namespace Horde.Build.IssueHandlers.Impl
{
	/// <summary>
	/// Instance of a particular compile error
	/// </summary>
	class SymbolIssueHandler : IIssueHandler
	{
		/// <inheritdoc/>
		public string Type => "Symbol";

		/// <inheritdoc/>
		public int Priority => 10;

		/// <summary>
		/// Determines if the given event id matches
		/// </summary>
		/// <param name="eventId">The event id to compare</param>
		/// <returns>True if the given event id matches</returns>
		public static bool IsMatchingEventId(EventId? eventId)
		{
			return eventId == KnownLogEvents.Linker_UndefinedSymbol || eventId == KnownLogEvents.Linker_DuplicateSymbol || eventId == KnownLogEvents.Linker;
		}

		/// <summary>
		/// Parses symbol names from a log event
		/// </summary>
		/// <param name="eventData">The log event data</param>
		/// <param name="symbolNames">Receives the list of symbol names</param>
		public static void GetSymbolNames(ILogEventData eventData, SortedSet<string> symbolNames)
		{
			foreach (ILogEventLine line in eventData.Lines)
			{
				string? identifier;
				if (line.Data.TryGetNestedProperty("properties.symbol.identifier", out identifier))
				{
					symbolNames.Add(identifier);
				}
			}
		}

		/// <inheritdoc/>
		public void RankSuspects(IIssueFingerprint fingerprint, List<SuspectChange> changes)
		{
			HashSet<string> names = new HashSet<string>();
			foreach (string name in fingerprint.Keys)
			{
				names.UnionWith(name.Split("::", StringSplitOptions.RemoveEmptyEntries));
			}

			foreach (SuspectChange change in changes)
			{
				if (change.ContainsCode)
				{
					int matches = names.Count(x => change.Details.Files.Any(y => y.Path.Contains(x, StringComparison.OrdinalIgnoreCase)));
					change.Rank += 10 + (10 * matches);
				}
			}
		}

		/// <inheritdoc/>
		public string GetSummary(IIssueFingerprint fingerprint, IssueSeverity severity)
		{
			HashSet<string> symbols = fingerprint.Keys;
			if (symbols.Count == 1)
			{
				return $"Undefined symbol '{symbols.First()}'";
			}
			else
			{
				return $"Undefined symbols: {StringUtils.FormatList(symbols.ToArray(), 3)}";
			}
		}

		public bool TryGetFingerprint(IJob job, INode node, ILogEventData eventData, [NotNullWhen(true)] out NewIssueFingerprint? fingerprint)
		{
			if (!IsMatchingEventId(eventData.EventId))
			{
				fingerprint = null;
				return false;
			}

			SortedSet<string> symbolNames = new SortedSet<string>();
			GetSymbolNames(eventData, symbolNames);

			fingerprint = new NewIssueFingerprint(Type, symbolNames, null);
			return true;
		}
	}
}
